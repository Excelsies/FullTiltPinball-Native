"""The pipe to the overlay program (overlay.c: shows a pane file over everything on the screen),
for cabinetd.py. Each command is answered "ok" or "error"; anything else is an OverlayError, and an
overlay that died is replaced first (the new one shows nothing), so the next command works again.
"""
import os
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))


class OverlayError(Exception):
    pass


class Overlay:
    def __init__(self, panes_dir, log):
        self.panes_dir, self.log = panes_dir, log
        self.proc = self.start()

    @staticmethod
    def start():
        return subprocess.Popen([os.path.join(HERE, "overlay")], stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, universal_newlines=True)

    def send(self, line):
        """OverlayError unless the overlay answers "ok"; one that died is replaced first (it shows nothing)."""
        try:
            self.proc.stdin.write(line + "\n")
            self.proc.stdin.flush()
            reply = self.proc.stdout.readline().strip()
        except (OSError, ValueError) as e:  # broken pipe / closed file: the overlay died
            self.log(f"overlay: {line}: {e}")
            reply = ""
        if reply == "ok":
            return
        if not reply:
            self.replace()
        raise OverlayError(f"overlay: {line} -> {reply or 'no reply (overlay died)'}")

    def replace(self):
        old = self.proc
        old.kill()
        try:
            rc = old.wait(timeout=2)
        except subprocess.TimeoutExpired:
            rc = "still running"
        for pipe in (old.stdin, old.stdout):
            try:
                pipe.close()
            except OSError:
                pass
        try:
            self.proc = self.start()
        except OSError as e:
            return self.log(f"overlay ended (rc {rc}); a new one could not start: {e}")
        self.log(f"overlay ended (rc {rc}); replaced by pid {self.proc.pid}")

    def show(self, name):
        self.send(f"show {self.panes_dir}/{name}.rgba")

    def toast(self, name):
        self.send(f"toast {self.panes_dir}/{name}.rgba")

    def hide(self):
        self.send("hide")
