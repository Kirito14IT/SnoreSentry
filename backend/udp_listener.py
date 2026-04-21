import socket
import threading
import time
import queue


def udp_receiver(ip="0.0.0.0", port=8888):
    """
    创建一个 UDP 服务器来接收 dB 数据。

    Args:
        ip (str): 服务器绑定的 IP 地址。"0.0.0.0" 表示监听所有可用接口。
        port (int): 服务器监听的端口号。
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server_address = (ip, port)

    try:
        sock.bind(server_address)
        print(f"UDP 接收器启动，监听地址: {server_address}")

        while True:
            # 接收数据和客户端地址
            data, client_address = sock.recvfrom(1024)  # 1024 字节缓冲区

            try:
                # 尝试解码为 UTF-8 字符串
                message = data.decode('utf-8')

                # 解析 dB 值 (假设格式为 "dB: -45.23")
                if message.startswith("dB: "):
                    try:
                        db_value_str = message.split(" ", 1)[1]  # 分割一次，取第二部分
                        db_value = float(db_value_str)
                        print(
                            f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] 收到来自 {client_address} 的 dB 数据: {db_value:.2f} dB")
                    except (IndexError, ValueError):
                        print(
                            f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] 收到来自 {client_address} 的消息，解析 dB 值失败: '{message}'")
                else:
                    # 如果不是标准 dB 格式，也打印出来
                    print(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] 收到来自 {client_address} 的消息: '{message}'")

            except UnicodeDecodeError:
                # 如果解码失败，打印原始字节
                print(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] 收到来自 {client_address} 的非文本数据: {data}")

    except KeyboardInterrupt:
        print("\n用户中断，关闭接收器...")
    except Exception as e:
        print(f"发生错误: {e}")
    finally:
        sock.close()


if __name__ == "__main__":
    # --- 请根据你的实际情况修改 IP 和端口 ---
    # 如果你的接收脚本运行在和开发板同一台电脑上，通常用 "0.0.0.0" 即可
    # 如果你想绑定到特定网卡，请使用该网卡的 IP 地址
    LISTEN_IP = "0.0.0.0"

    # 这个端口必须和 C 代码里的 DB_SEND_TARGET_PORT 一致
    LISTEN_PORT = 8888

    udp_receiver(LISTEN_IP, LISTEN_PORT)