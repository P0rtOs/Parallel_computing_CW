import socket
import time
import random
from locust import User, task, between, events


HOST = "127.0.0.1"
PORT = 8080

WORDS = ["good", "bad", "movie", "film", "great", "love", "hate"]
AND_QUERIES = ["good movie", "great film", "love movie", "bad film"]
OR_QUERIES = ["good bad", "love hate", "great bad", "movie film"]


def tcp_request(command: str, timeout: float = 10.0) -> str:
    data = (command.strip() + "\n").encode("utf-8")
    with socket.create_connection((HOST, PORT), timeout=timeout) as sock:
        sock.sendall(data)
        chunks = []
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            chunks.append(chunk.decode("utf-8", errors="replace"))
    return "".join(chunks)


class TcpSearchUser(User):
    wait_time = between(0.01, 0.2)  # пауза між запитами (імітація реальних юзерів)

    def _measure(self, name: str, command: str):
        start = time.perf_counter()
        exc = None
        resp = ""
        try:
            resp = tcp_request(command)
            if not resp.strip().startswith("OK"):
                raise RuntimeError(f"Bad response: {resp[:200]}")
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

    SAMPLE_FILE1 = "test1.txt"
    SAMPLE_FILE2 = "test2.txt"

    @task(10)
    def has_file(self):
        self._measure("HAS_FILE", f"HAS_FILE {self.SAMPLE_FILE1}")
    
    @task(10)
    def reindex_file(self):
        self._measure("REINDEX_FILE", f"REINDEX_FILE {self.SAMPLE_FILE2}")
