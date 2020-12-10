#include "TcpForward.h"
#include "../logging/LoggerFactory.h"
#include <aws/crt/io/SocketOptions.h>

using namespace std;
using namespace Aws::Iot::DeviceClient::Logging;

namespace Aws
{
    namespace Iot
    {
        namespace DeviceClient
        {
            namespace SecureTunneling
            {
                constexpr char TcpForward::TAG[];

                TcpForward::TcpForward(
                    std::shared_ptr<SharedCrtResourceManager> sharedCrtResourceManager,
                    uint16_t port,
                    OnTcpForwardDataReceive onTcpForwardDataReceive)
                {
                    mSharedCrtResourceManager = sharedCrtResourceManager;
                    mPort = port;
                    mOnTcpForwardDataReceive = onTcpForwardDataReceive;

                    AWS_ZERO_STRUCT(mSocket);
                    Aws::Crt::Io::SocketOptions socketOptions;
                    aws_socket_init(&mSocket, sharedCrtResourceManager->getAllocator(), &socketOptions.GetImpl());
                }

                TcpForward::~TcpForward() { aws_socket_clean_up(&mSocket); }

                int TcpForward::Connect()
                {
                    aws_socket_endpoint endpoint{};
                    string localhost = "127.0.0.1";
                    snprintf(endpoint.address, AWS_ADDRESS_MAX_LEN, "%s", localhost.c_str());
                    endpoint.port = mPort;

                    aws_event_loop *eventLoop = aws_event_loop_group_get_next_loop(
                        mSharedCrtResourceManager->getEventLoopGroup()->GetUnderlyingHandle());

                    aws_socket_connect(&mSocket, &endpoint, eventLoop, sOnConnectionResult, this);

                    return 0;
                }

                int TcpForward::Close()
                {
                    aws_socket_close(&mSocket);
                    return 0;
                }

                int TcpForward::SendData(const Crt::ByteCursor &data)
                {
                    aws_socket_write(&mSocket, &data, sOnWriteCompleted, this);
                    return 0;
                }

                void TcpForward::sOnConnectionResult(struct aws_socket *socket, int error_code, void *user_data)
                {
                    auto *self = static_cast<TcpForward *>(user_data);
                    self->OnConnectionResult(socket, error_code);
                }

                void TcpForward::sOnWriteCompleted(
                    struct aws_socket *socket,
                    int error_code,
                    size_t bytes_written,
                    void *user_data)
                {
                    auto *self = static_cast<TcpForward *>(user_data);
                    self->OnWriteCompleted(socket, error_code, bytes_written);
                }

                void TcpForward::sOnReadable(struct aws_socket *socket, int error_code, void *user_data)
                {
                    auto *self = static_cast<TcpForward *>(user_data);
                    self->OnReadable(socket, error_code);
                }

                void TcpForward::OnConnectionResult(struct aws_socket *socket, int error_code)
                {
                    LOG_DEBUG(TAG, "TcpForward::OnConnectionResult");
                    if (error_code)
                    {
                        LOGM_ERROR(TAG, "TcpForward::OnConnectionResult error_code=%d", error_code);
                    }
                    else
                    {
                        aws_socket_subscribe_to_readable_events(&mSocket, sOnReadable, this);
                    }
                }

                void TcpForward::OnWriteCompleted(struct aws_socket *socket, int error_code, size_t bytes_written)
                {
                    if (error_code)
                    {
                        LOGM_ERROR(
                            TAG,
                            "TcpForward::OnWriteCompleted error_code=%d, bytes_written=%d",
                            error_code,
                            bytes_written);
                    }
                }

                void TcpForward::OnReadable(struct aws_socket *socket, int error_code)
                {
                    LOGM_DEBUG(TAG, "TcpForward::OnReadable error_code=%d", error_code);

                    Aws::Crt::ByteBuf everything; // For cumulating everything available
                    aws_byte_buf_init(&everything, mSharedCrtResourceManager->getAllocator(), 0);

                    constexpr size_t chunkCapacity = 1024;
                    Aws::Crt::ByteBuf chunk;
                    aws_byte_buf_init(&chunk, mSharedCrtResourceManager->getAllocator(), chunkCapacity);
                    size_t amountRead = 0;
                    do
                    {
                        amountRead = 0;
                        if (aws_socket_read(&mSocket, &chunk, &amountRead) == AWS_OP_SUCCESS && amountRead > 0)
                        {
                            aws_byte_cursor chunkCursor = aws_byte_cursor_from_buf(&chunk);
                            aws_byte_buf_append_dynamic(&everything, &chunkCursor);
                        }
                    } while (amountRead > 0);

                    // Send everything
                    mOnTcpForwardDataReceive(everything);

                    aws_byte_buf_clean_up(&chunk);
                    aws_byte_buf_clean_up(&everything);
                }

            } // namespace SecureTunneling
        }     // namespace DeviceClient
    }         // namespace Iot
} // namespace Aws