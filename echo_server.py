import socket

HOST = '0.0.0.0' 
PORT = 1234

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.bind((HOST, PORT))
    s.listen()
    conn, addr = s.accept()
    with conn:
        while True:
            data = conn.recv(1024)
            print(data.decode())
            conn.sendall(data)
            conn, addr = s.accept()