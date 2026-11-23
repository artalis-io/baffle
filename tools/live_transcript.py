"""Local external live-caption demo: installed Hail -> Baffle -> offline ASR.

This is bounded buffered re-decoding, not native streaming ASR or a dialogue
runtime. No audio/transcript is saved. Python/Tk/ASR are optional demo dependencies.
"""
import argparse
import collections
import pathlib
import queue
import struct
import subprocess
import threading
import time
import tkinter as tk
from tkinter import ttk

HEADER = struct.Struct('<IIIIQQQff')
MAGIC = 0x564c4642
RATE = 16000
FRAMES = 160


def latest(q, item):
    try:
        q.put_nowait(item)
    except queue.Full:
        try:
            q.get_nowait()
        except queue.Empty:
            pass
        q.put_nowait(item)


def read_exact(stream, size):
    data = bytearray()
    while len(data) < size:
        part = stream.read(size - len(data))
        if not part:
            if data:
                raise ValueError('Truncated live audio packet')
            return None
        data.extend(part)
    return bytes(data)


class Session:
    def __init__(self, args, events):
        self.args, self.events = args, events
        self.stop = threading.Event()
        self.audio = queue.Queue(maxsize=100)  # At most one second pending.
        self.process = None
        self.dropped = 0
        self.level = 0.0
        self.lost = 0
        self.error = ''
        self.thread = threading.Thread(target=self.run, daemon=True)

    def reader(self):
        import numpy as np
        try:
            while True:
                header = read_exact(self.process.stdout, HEADER.size)
                if header is None:
                    break
                magic, frames, flags, vad, position, epoch, lost, rms, peak = HEADER.unpack(header)
                if magic != MAGIC or frames != FRAMES:
                    raise ValueError('Invalid live audio packet')
                body = read_exact(self.process.stdout, frames * 8)
                if body is None:
                    raise ValueError('Missing audio packet')
                samples = np.frombuffer(body, dtype='<f4').reshape(2, frames)
                if not np.all(np.isfinite(samples)):
                    raise ValueError('Nonfinite audio')
                self.level, self.lost = rms, lost
                chunk = samples[0 if self.args.raw else 1].copy()
                if self.audio.full():
                    try:
                        self.audio.get_nowait()
                        self.dropped += 1
                    except queue.Empty:
                        pass
                self.audio.put_nowait((chunk, rms, flags, position, epoch, lost, self.dropped))
        except Exception as exc:
            self.error = str(exc)
        finally:
            latest(self.audio, None)

    def run(self):
        import numpy as np
        from inference_contention import load_asr
        reader = None
        try:
            latest(self.events, ('status', 'Loading local speech model…'))
            recognizer = load_asr(self.args.model, self.args.models)
            if self.stop.is_set():
                return
            command = [str(self.args.capture.resolve()), '3', self.args.device, '1800']
            self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                            stderr=subprocess.PIPE, bufsize=0,
                                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            reader = threading.Thread(target=self.reader, daemon=True)
            reader.start()
            latest(self.events, ('status', 'Listening — speak into your Shure mic'))
            pre = collections.deque(maxlen=25)  # 250 ms of onset context.
            utterance = collections.deque(maxlen=800)  # Eight-second decode limit.
            active = False
            silent = 0
            draft = ''
            last_decode = 0.0
            previous = None

            def decode():
                samples = np.concatenate(tuple(utterance))
                stream = recognizer.create_stream()
                stream.accept_waveform(RATE, samples)
                began = time.perf_counter()
                recognizer.decode_stream(stream)
                return stream.result.text.strip(), (time.perf_counter() - began) * 1000

            while not self.stop.is_set():
                try:
                    packet = self.audio.get(timeout=.1)
                except queue.Empty:
                    if not reader.is_alive():
                        break
                    continue
                if packet is None:
                    break
                chunk, rms, flags, position, epoch, lost, dropped = packet
                if previous is not None and (position != previous[0] + FRAMES or epoch != previous[1]
                                              or lost != previous[2] or dropped != previous[3]
                                              or flags & (16 | 32)):
                    # Never concatenate across loss/restarts as uninterrupted speech.
                    utterance.clear()
                    pre.clear()
                    active = False
                    silent = 0
                    draft = ''
                    latest(self.events, ('draft', ''))
                    latest(self.events, ('status', 'Audio gap — listening resumed'))
                previous = position, epoch, lost, dropped
                # Only demo endpointing. This energy threshold is not user attribution.
                voiced = rms >= .002
                if not active:
                    pre.append(chunk)
                    if not voiced:
                        continue
                    utterance.extend(pre)
                    pre.clear()
                    active = True
                else:
                    utterance.append(chunk)
                silent = 0 if voiced else silent + 1
                now = time.monotonic()
                finish = silent >= 90 or len(utterance) >= 800
                if len(utterance) >= 35 and (finish or now - last_decode >= .6):
                    draft, milliseconds = decode()
                    last_decode = time.monotonic()
                    latest(self.events, ('draft', draft))
                    latest(self.events, ('status', f'Listening · last decode {milliseconds:.0f} ms'))
                if finish:
                    if draft:
                        latest(self.events, ('final', draft))
                    latest(self.events, ('draft', ''))
                    utterance.clear()
                    pre.clear()
                    active = False
                    silent = 0
                    draft = ''
            if self.error:
                raise RuntimeError(self.error)
        except Exception as exc:
            latest(self.events, ('status', 'Error: ' + str(exc)))
        finally:
            if self.process is not None:
                if self.process.poll() is None:
                    try:
                        self.process.stdin.write(b'\n')
                        self.process.stdin.flush()
                    except (BrokenPipeError, OSError):
                        pass
                try:
                    self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.process.terminate()
                    self.process.wait(timeout=3)
                if reader is not None:
                    reader.join(timeout=2)
                error = self.process.stderr.read().decode('utf-8', errors='replace').strip()
                if self.process.returncode:
                    latest(self.events, ('status', 'Capture stopped: ' + error[-400:]))
                for pipe in (self.process.stdin, self.process.stdout, self.process.stderr):
                    pipe.close()
            latest(self.events, ('done', None))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=pathlib.Path)
    parser.add_argument('models', type=pathlib.Path)
    parser.add_argument('--device', required=True)
    parser.add_argument('--model', choices=('whisper', 'parakeet'), default='parakeet')
    parser.add_argument('--raw', action='store_true')
    args = parser.parse_args()
    if not args.capture.is_file() or not args.models.is_dir():
        parser.error('Capture executable and local models must exist')
    root = tk.Tk()
    root.title('Live transcript — Shure MV7+')
    root.geometry('1080x740')
    root.configure(bg='#101820')
    root.minsize(720, 500)
    root.lift()
    root.attributes('-topmost', True)
    root.after(2000, lambda: root.attributes('-topmost', False))
    status = tk.StringVar(value='Starting…')
    tk.Label(root, text='Live transcript', font=('Segoe UI', 24, 'bold'), fg='#f0f6fa', bg='#101820').pack(anchor='w', padx=28, pady=(24, 6))
    route = 'Raw microphone' if args.raw else 'Baffle noise suppression + gain control'
    tk.Label(root, text=f'Shure MV7+  ·  {route}  ·  {args.model.capitalize()} / English',
             fg='#9cb2c4', bg='#101820', font=('Segoe UI', 11)).pack(anchor='w', padx=28)
    tk.Label(root, textvariable=status, fg='#65dbc8', bg='#101820', font=('Segoe UI', 12)).pack(anchor='w', padx=28, pady=(16, 8))
    meter = ttk.Progressbar(root, maximum=1, length=400)
    meter.pack(fill='x', padx=28, pady=(0, 12))
    tk.Label(root, text='LIVE DRAFT — words may change as you speak', fg='#9cb2c4', bg='#101820', font=('Segoe UI', 10)).pack(anchor='w', padx=28)
    draft = tk.StringVar(value='Speak when the status says Listening.')
    tk.Label(root, textvariable=draft, wraplength=990, justify='left', anchor='nw', height=4,
             font=('Segoe UI', 23), fg='#ffffff', bg='#1a2936').pack(fill='x', padx=28, pady=(8, 16))
    tk.Label(root, text='Recent speech', fg='#9cb2c4', bg='#101820', font=('Segoe UI', 11)).pack(anchor='w', padx=28)
    history = tk.Text(root, height=9, wrap='word', font=('Segoe UI', 15), bg='#15212d', fg='#e2ecf4',
                      relief='flat', padx=12, pady=12, state='disabled')
    history.pack(fill='both', expand=True, padx=28, pady=(8, 12))
    footer = tk.Frame(root, bg='#101820')
    footer.pack(fill='x', padx=28, pady=(0, 20))
    tk.Label(footer, text='Local processing · no audio saved · no spoken replies\nRevisable buffered captions; playback echo may also be transcribed.',
             justify='left', font=('Segoe UI', 10), fg='#9cb2c4', bg='#101820').pack(side='left')
    events = queue.Queue(maxsize=32)
    session = Session(args, events)
    stopped = False
    closing = False
    lines = collections.deque(maxlen=60)

    def stop():
        nonlocal stopped
        stopped = True
        session.stop.set()
        status.set('Stopping microphone…')
        button.config(state='disabled')

    def close():
        nonlocal closing
        closing = True
        stop()

    button = tk.Button(footer, text='Stop microphone', command=stop, font=('Segoe UI', 12), padx=18, pady=8)
    button.pack(side='right')
    root.protocol('WM_DELETE_WINDOW', close)

    def poll():
        nonlocal stopped
        for _ in range(32):
            try:
                kind, text = events.get_nowait()
            except queue.Empty:
                break
            if kind == 'draft':
                draft.set(text or '…')
            elif kind == 'status':
                status.set(text)
            elif kind == 'final':
                lines.append(text)
                history.config(state='normal')
                history.delete('1.0', 'end')
                history.insert('end', '\n\n'.join(lines))
                history.see('end')
                history.config(state='disabled')
            elif kind == 'done':
                if stopped:
                    status.set('Microphone stopped')
                button.config(state='disabled')
        meter['value'] = min(1, session.level / .08)
        if session.lost or session.dropped:
            root.title(f'Live transcript — audio loss: {session.lost} frames / {session.dropped} queued blocks')
        if closing and not session.thread.is_alive():
            root.destroy()
            return
        root.after(100, poll)

    session.thread.start()
    root.after(100, poll)
    root.mainloop()


if __name__ == '__main__':
    main()
