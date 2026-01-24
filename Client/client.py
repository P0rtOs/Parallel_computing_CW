import socket
import sys
from typing import List, Tuple, Optional

HOST = "127.0.0.1"
PORT = 8080


def _read_until_end(sock: socket.socket, timeout: float = 10.0) -> str:
    sock.settimeout(timeout)
    buf = ""
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            break
        buf += chunk.decode("utf-8", errors="replace")
        if "\nEND\n" in buf or buf.endswith("END\n"):
            break
    return buf


def send_request(command: str, timeout: float = 10.0) -> str:
    data = (command.strip() + "\n").encode("utf-8")
    with socket.create_connection((HOST, PORT), timeout=timeout) as sock:
        sock.sendall(data)
        return _read_until_end(sock, timeout=timeout)


def parse_response(resp: str) -> Tuple[str, List[str]]:
    lines = resp.splitlines()
    clean = [ln.rstrip("\r") for ln in lines if ln.strip() != ""]
    if not clean:
        return "ERROR Empty response", []

    status = clean[0].strip()
    payload = []
    for ln in clean[1:]:
        ln = ln.strip()
        if ln == "END":
            break
        payload.append(ln)
    return status, payload


def parse_ok_count(status_line: str) -> Optional[int]:
    parts = status_line.split()
    if len(parts) >= 2 and parts[0] == "OK":
        try:
            return int(parts[1])
        except ValueError:
            return None
    return None


def pretty_print_search(resp: str):
    status, payload = parse_response(resp)
    if not status.startswith("OK"):
        print("=== SERVER RESPONSE (RAW) ===")
        print(resp)
        return

    n = parse_ok_count(status)
    if n is None:
        print("=== SERVER RESPONSE (RAW) ===")
        print(resp)
        return

    print(f"OK. Знайдено {n} документ(ів).")
    if not payload:
        return
    for p in payload:
        print("  ", p)


def pretty_print_generic(resp: str):
    status, payload = parse_response(resp)
    print(f"STATUS: {status}")
    if payload:
        print("PAYLOAD:")
        for ln in payload:
            print("  ", ln)


def print_menu():
    print()
    print("=== Меню клієнта ===")
    print("1) SEARCH_ONE <word>")
    print("2) SEARCH_ALL <w1 w2 ...>   (AND)")
    print("3) SEARCH_ANY <w1 w2 ...>   (OR)")
    print("4) ADD_FILE <relative_or_abs_path>")
    print("5) REMOVE_FILE <relative_or_abs_path>")
    print("6) REINDEX_FILE <relative_or_abs_path>")
    print("7) HAS_FILE <relative_or_abs_path>")
    print("8) INDEX_INC                 (інкрементальна індексація)")
    print("9) INDEX_ALL                  (повний rebuild базової директорії сервера)")
    print("h) help")
    print("0) exit")
    print()

def build_command(choice: str) -> Optional[str]:
    c = choice.strip()
    if not c:
        return None

    if c in ("0", "exit", "quit"):
        return "EXIT"

    if c in ("h", "help", "?"):
        return "HELP"

    if any(ch.isalpha() for ch in c):
        return c

    if c == "1":
        word = input("Введи слово: ").strip()
        return f"SEARCH_ONE {word}" if word else None
    if c == "2":
        line = input("Введи слова через пробіл (AND): ").strip()
        return f"SEARCH_ALL {line}" if line else None
    if c == "3":
        line = input("Введи слова через пробіл (OR): ").strip()
        return f"SEARCH_ANY {line}" if line else None
    if c == "4":
        path = input("Введи шлях (relative/abs): ").strip()
        return f"ADD_FILE {path}" if path else None
    if c == "5":
        path = input("Введи шлях (relative/abs): ").strip()
        return f"REMOVE_FILE {path}" if path else None
    if c == "6":
        path = input("Введи шлях (relative/abs): ").strip()
        return f"REINDEX_FILE {path}" if path else None
    if c == "7":
        path = input("Введи шлях (relative/abs): ").strip()
        return f"HAS_FILE {path}" if path else None
    if c == "8":
        return "INDEX_INC"
    if c == "9":
        return "INDEX_ALL"

    return None


def main():
    print(f"TCP client for {HOST}:{PORT}")
    print_menu()

    while True:
        try:
            choice = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\nВихід.")
            return

        cmd = build_command(choice)
        if cmd is None:
            print("Невірний ввід. 'h' для help.")
            continue

        if cmd == "HELP":
            print_menu()
            continue

        if cmd == "EXIT":
            print("Вихід.")
            return

        if not cmd.strip():
            continue

        try:
            resp = send_request(cmd, timeout=10.0)
        except Exception as e:
            print(f"[NETWORK ERROR] {e}")
            continue

        upper = cmd.strip().upper()
        if upper.startswith("SEARCH_ONE") or upper.startswith("SEARCH_ALL") or upper.startswith("SEARCH_ANY"):
            pretty_print_search(resp)
        else:
            pretty_print_generic(resp)


if __name__ == "__main__":
    main()
