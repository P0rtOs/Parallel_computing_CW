import socket
import time
import random
from typing import Tuple, Optional
from locust import User, task, between, events

HOST = "127.0.0.1"
PORT = 8080

WORDS = ["good", "bad", "movie", "film", "great", "love", "hate"]
AND_QUERIES = ["good movie", "great film", "love movie", "bad film"]
OR_QUERIES = ["good bad", "love hate", "great bad", "movie film"]

# Файли відносно baseDir сервера (text_files/aclImdb/...)
# Тут приклад — підстав свої реальні, які точно існують.
SEED_FILES = [
    "train/pos/4251_9.txt",
    "train/neg/4251_1.txt",
    "test/pos/4251_8.txt",
    "test/neg/4251_2.txt",
]

# Для періодичних add/remove/reindex
MUTATE_FILES = [
    "train/pos/4252_9.txt",
    "train/neg/4252_1.txt",
    "test/pos/4252_8.txt",
    "test/neg/4252_2.txt",
]

def _read_until_end(sock: socket.socket, timeout: float = 10.0) -> str:
    """
    Сервер тепер повертає END\\n — читаємо до нього.
    """
    sock.settimeout(timeout)
    buf = ""
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            # якщо сервер закрив — теж ок
            break
        buf += chunk.decode("utf-8", errors="replace")
        if "\nEND\n" in buf or buf.endswith("END\n"):
            break
    return buf

def tcp_request(command: str, timeout: float = 10.0) -> str:
    data = (command.strip() + "\n").encode("utf-8")
    with socket.create_connection((HOST, PORT), timeout=timeout) as sock:
        sock.sendall(data)
        return _read_until_end(sock, timeout=timeout)

def parse_status(resp: str) -> Tuple[bool, Optional[str]]:
    """
    Повертає (ok, err_msg).
    Формат: OK ... або ERROR ...
    """
    head = resp.strip().splitlines()[0] if resp.strip() else ""
    if head.startswith("OK"):
        return True, None
    if head.startswith("ERROR"):
        return False, head
    return False, f"Bad response head: {head[:200]}"

class TcpSearchUser(User):
    wait_time = between(0.01, 0.2)

    # щоб не засмічувати mutate-операціями занадто часто
    _did_seed = False

    def on_start(self):
        """
        Раз на юзера проіндексуємо кілька файлів, щоб HAS_FILE/REINDEX не були "порожні".
        (ADD_FILE поверне ERROR якщо вже є — це нормально, ми ігноруємо)
        """
        if self._did_seed:
            return
        self._did_seed = True

        for rel in SEED_FILES:
            self._measure("ADD_FILE(SEED)", f"ADD_FILE {rel}", allow_error=True)

    def _measure(self, name: str, command: str, allow_error: bool = False):
        start = time.perf_counter()
        exc = None
        resp = ""
        try:
            resp = tcp_request(command)

            ok, err = parse_status(resp)
            if not ok and not allow_error:
                raise RuntimeError(err or f"Bad response: {resp[:200]}")
        except Exception as e:
            exc = e

        elapsed_ms = (time.perf_counter() - start) * 1000.0
        events.request.fire(
            request_type="TCP",
            name=name,
            response_time=elapsed_ms,
            response_length=len(resp),
            exception=exc,
        )

    # ---------------- SEARCH ----------------

    @task(40)
    def search_one(self):
        w = random.choice(WORDS)
        self._measure("SEARCH_ONE", f"SEARCH_ONE {w}")

    @task(15)
    def search_all(self):
        q = random.choice(AND_QUERIES)
        self._measure("SEARCH_ALL", f"SEARCH_ALL {q}")

    @task(15)
    def search_any(self):
        q = random.choice(OR_QUERIES)
        self._measure("SEARCH_ANY", f"SEARCH_ANY {q}")

    # ---------------- INDEX MUTATIONS ----------------

    @task(8)
    def has_file(self):
        rel = random.choice(SEED_FILES + MUTATE_FILES)
        self._measure("HAS_FILE", f"HAS_FILE {rel}", allow_error=False)

    @task(6)
    def add_file(self):
        rel = random.choice(MUTATE_FILES)
        # якщо вже є — буде ERROR "already indexed" (це норм під навантаженням)
        self._measure("ADD_FILE", f"ADD_FILE {rel}", allow_error=True)

    @task(6)
    def reindex_file(self):
        rel = random.choice(SEED_FILES + MUTATE_FILES)
        self._measure("REINDEX_FILE", f"REINDEX_FILE {rel}", allow_error=True)

    @task(4)
    def remove_file(self):
        rel = random.choice(MUTATE_FILES)
        # якщо нема — буде ERROR, це теж норм
        self._measure("REMOVE_FILE", f"REMOVE_FILE {rel}", allow_error=True)

    @task(2)
    def index_inc(self):
        # важча операція — рідко
        self._measure("INDEX_INC", "INDEX_INC", allow_error=False)
