import socket

HOST = "127.0.0.1"
PORT = 8080

def send_request(command: str) -> str:
    data = command.strip() + "\n"

    with socket.create_connection((HOST, PORT)) as sock:
        sock.sendall(data.encode("utf-8"))

        chunks = []
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            chunks.append(chunk.decode("utf-8", errors="replace"))

    return "".join(chunks)


def parse_response(resp: str):
    lines = [line.strip() for line in resp.splitlines() if line.strip()]

    if not lines:
        return False, []

    if not lines[0].startswith("OK"):
        return False, []

    if len(lines) == 1:
        return True, []

    paths = []
    for line in lines[1:]:
        if line == "END":
            break
        paths.append(line)

    return True, paths


def search_one(word: str):
    resp = send_request(f"SEARCH_ONE {word}")
    ok, paths = parse_response(resp)
    return paths


def search_all(words):
    resp = send_request("SEARCH_ALL " + " ".join(words))
    ok, paths = parse_response(resp)
    return paths


def search_any(words):
    resp = send_request("SEARCH_ANY " + " ".join(words))
    ok, paths = parse_response(resp)
    return paths


if __name__ == "__main__":
    while True:
        query = input("Enter word (or 'quit'): ").strip()
        if query == "quit":
            break

        results = search_one(query)
        print(f"Found {len(results)} docs:")
        for p in results:
            print("  ", p)
