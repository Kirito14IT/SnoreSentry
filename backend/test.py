import socket

def udp_client():
    # 服务器配置（根据实际日志调整）
    SERVER_IP = "192.168.118.149"  # 服务器IP
    SERVER_PORT = 9988             # 服务器端口
    BUFFER_SIZE = 2048             # 缓冲区大小（大于1030字节确保接收完整）

    # 创建UDP Socket
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    # 可选：绑定本地端口（不绑定则系统自动分配）
    client_socket.bind(("0.0.0.0", 0))  # 监听所有网卡的随机端口

    print(f"UDP客户端已启动，等待接收来自 {SERVER_IP}:{SERVER_PORT} 的雷达数据...")

    try:
        while True:
            # 接收数据和来源地址
            data, addr = client_socket.recvfrom(BUFFER_SIZE)
            data_len = len(data)
            print(f"收到来自 {addr} 的数据，长度: {data_len} 字节")

            # 尝试解码为文本（如果是JSON等文本数据）
            try:
                decoded_data = data.decode("utf-8")
                print("文本数据内容:", decoded_data)
            except UnicodeDecodeError:
                # 若为二进制数据（如雷达原始数据），打印十六进制
                hex_data = data.hex(" ", 1)  # 每字节用空格分隔
                print("二进制数据（十六进制）:", hex_data[:500])  # 限制打印前500字符避免刷屏

    except KeyboardInterrupt:
        print("客户端已停止")
    finally:
        client_socket.close()

if __name__ == "__main__":
    udp_client()