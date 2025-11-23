import socket

HOST = "127.0.0.1"
PORT = 8080


def send_request(command: str) -> str:
    """
    Відправляє один текстовий запит на сервер і повертає всю відповідь (як строку).
    command: без \n в кінці, ми самі його додамо.
    """
    data = (command.strip() + "\n").encode("utf-8")

    with socket.create_connection((HOST, PORT)) as sock:
        sock.sendall(data)

        chunks = []
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            chunks.append(chunk.decode("utf-8", errors="replace"))

    return "".join(chunks)


def parse_search_response(resp: str):
    """
    Парсить відповідь виду:

        OK N
        path1
        path2
        ...
        END

    Повертає (ok: bool, paths: list[str])
    """
    lines = [line.strip() for line in resp.splitlines() if line.strip()]

    if not lines:
        return False, []

    first = lines[0]
    if not first.startswith("OK"):
        return False, []

    if len(lines) == 1:
        return True, []

    paths = []
    for line in lines[1:]:
        if line == "END":
            break
        paths.append(line)

    return True, paths


def action_search_one():
    word = input("Введи слово для SEARCH_ONE: ").strip()
    if not word:
        print("Слово не може бути порожнім.")
        return
    resp = send_request(f"SEARCH_ONE {word}")
    ok, paths = parse_search_response(resp)
    if not ok:
        print("Помилка або пустий результат. Сирий респонс:")
        print(resp)
        return
    print(f"Знайдено {len(paths)} документ(ів):")
    for p in paths:
        print("  ", p)


def action_search_all():
    line = input("Введи слова через пробіл для SEARCH_ALL (логічне AND): ").strip()
    if not line:
        print("Список слів не може бути порожнім.")
        return
    resp = send_request("SEARCH_ALL " + line)
    ok, paths = parse_search_response(resp)
    if not ok:
        print("Помилка або пустий результат. Сирий респонс:")
        print(resp)
        return
    print(f"Знайдено {len(paths)} документ(ів):")
    for p in paths:
        print("  ", p)


def action_search_any():
    line = input("Введи слова через пробіл для SEARCH_ANY (логічне OR): ").strip()
    if not line:
        print("Список слів не може бути порожнім.")
        return
    resp = send_request("SEARCH_ANY " + line)
    ok, paths = parse_search_response(resp)
    if not ok:
        print("Помилка або пустий результат. Сирий респонс:")
        print(resp)
        return
    print(f"Знайдено {len(paths)} документ(ів):")
    for p in paths:
        print("  ", p)


def action_add_file():
    path = input("Введи повний шлях до файлу для ADD_FILE: ").strip()
    if not path:
        print("Шлях не може бути порожнім.")
        return
    resp = send_request(f"ADD_FILE {path}")
    print("Відповідь сервера:")
    print(resp)


def action_remove_file():
    path = input("Введи шлях до файлу для REMOVE_FILE: ").strip()
    if not path:
        print("Шлях не може бути порожнім.")
        return
    resp = send_request(f"REMOVE_FILE {path}")
    print("Відповідь сервера:")
    print(resp)


def action_reindex_file():
    path = input("Введи шлях до файлу для REINDEX_FILE: ").strip()
    if not path:
        print("Шлях не може бути порожнім.")
        return
    resp = send_request(f"REINDEX_FILE {path}")
    print("Відповідь сервера:")
    print(resp)


def action_has_file():
    path = input("Введи шлях до файлу для HAS_FILE: ").strip()
    if not path:
        print("Шлях не може бути порожнім.")
        return
    resp = send_request(f"HAS_FILE {path}")
    print("Відповідь сервера:")
    print(resp)


def print_menu():
    print()
    print("=== Меню клієнта ===")
    print("1) SEARCH_ONE (пошук по одному слову)")
    print("2) SEARCH_ALL (усі слова, AND)")
    print("3) SEARCH_ANY (будь-яке слово, OR)")
    print("4) ADD_FILE")
    print("5) REMOVE_FILE")
    print("6) REINDEX_FILE")
    print("7) HAS_FILE")
    print("0) Вихід")


def main():
    while True:
        print_menu()
        choice = input("Обери команду: ").strip()

        if choice == "0":
            print("Вихід.")
            break
        elif choice == "1":
            action_search_one()
        elif choice == "2":
            action_search_all()
        elif choice == "3":
            action_search_any()
        elif choice == "4":
            action_add_file()
        elif choice == "5":
            action_remove_file()
        elif choice == "6":
            action_reindex_file()
        elif choice == "7":
            action_has_file()
        else:
            print("Невірний вибір, спробуй ще раз.")


if __name__ == "__main__":
    main()
