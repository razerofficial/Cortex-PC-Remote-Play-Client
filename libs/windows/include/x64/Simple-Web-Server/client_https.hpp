#ifndef SIMPLE_WEB_CLIENT_HTTPS_HPP
#define SIMPLE_WEB_CLIENT_HTTPS_HPP

#include "client_http.hpp"

#ifdef USE_STANDALONE_ASIO
#include <asio/ssl.hpp>
#else
#include <boost/asio/ssl.hpp>
#endif

namespace SimpleWeb {
  using HTTPS = asio::ssl::stream<asio::ip::tcp::socket>;

  template <>
  class Client<HTTPS> : public ClientBase<HTTPS> {
  public:
    /**
     * Constructs a client object.
     *
     * @param server_port_path   Server resource given by host[:port][/path]
     * @param verify_certificate Set to true (default) to verify the server's certificate and hostname according to RFC 2818.
     * @param certification_file If non-empty, sends the given certification file to server. Requires private_key_file.
     * @param private_key_file   If non-empty, specifies the file containing the private key for certification_file. Requires certification_file.
     * @param verify_file        If non-empty, use this certificate authority file to perform verification.
     */
    Client(const std::string &server_port_path, bool verify_certificate = true, const std::string &certification_file = std::string(),
           const std::string &private_key_file = std::string(), const std::string &verify_file = std::string())
        : ClientBase<HTTPS>::ClientBase(server_port_path, 443), context(asio::ssl::context::tlsv12) {
      if(certification_file.size() > 0 && private_key_file.size() > 0) {
        context.use_certificate_chain_file(certification_file);
        context.use_private_key_file(private_key_file, asio::ssl::context::pem);
      }

      if(verify_certificate)
        context.set_verify_callback(asio::ssl::rfc2818_verification(host));

      if(verify_file.size() > 0)
        context.load_verify_file(verify_file);
      else
        context.set_default_verify_paths();

      if(verify_certificate)
        context.set_verify_mode(asio::ssl::verify_peer);
      else
        context.set_verify_mode(asio::ssl::verify_none);
    }

    // 添加一个公共方法来从字符串设置证书和私钥
    bool set_certificate_from_string(const std::string& cert_content, const std::string& key_content) {
        return load_cert_from_string(context, cert_content, key_content);
    }

  protected:
    asio::ssl::context context;

    std::shared_ptr<Connection> create_connection() noexcept override {
      return std::make_shared<Connection>(handler_runner, *io_service, context);
    }

    // 辅助函数来从字符串加载证书和私钥
    static bool load_cert_from_string(asio::ssl::context& context, const std::string& cert_content, const std::string& key_content) {
        // Create memory BIO for certificate
        BIO* bio_cert = BIO_new_mem_buf(cert_content.c_str(), -1);
        if (bio_cert == nullptr) {
            fprintf(stderr, "Failed to create memory BIO for certificate.\n");
            return false;
        }

        // Load certificate into X509 structure
        X509* cert = PEM_read_bio_X509(bio_cert, nullptr, nullptr, nullptr);
        BIO_free(bio_cert); // Free the BIO after reading the certificate
        if (cert == nullptr) {
            fprintf(stderr, "Failed to load certificate from string.\n");
            return false;
        }

        // Convert X509 to PEM format and write to buffer
        BIO* pem_bio_cert = BIO_new(BIO_s_mem());
        if (PEM_write_bio_X509(pem_bio_cert, cert) <= 0) {
            BIO_free_all(pem_bio_cert);
            X509_free(cert);
            fprintf(stderr, "Failed to convert certificate to PEM format.\n");
            return false;
        }

        BUF_MEM* mem_ptr;
        BIO_get_mem_ptr(pem_bio_cert, &mem_ptr);
        BIO_set_close(pem_bio_cert, BIO_NOCLOSE);
        BIO_free_all(pem_bio_cert);

        // Add the certificate to the context using a const_buffer
        if (!set_context_certificate(context, asio::buffer(mem_ptr->data, mem_ptr->length))) {
            OPENSSL_free(mem_ptr->data);
            X509_free(cert);
            return false;
        }
        OPENSSL_free(mem_ptr->data);
        X509_free(cert);

        // Create memory BIO for private key
        BIO* bio_key = BIO_new_mem_buf(key_content.c_str(), -1);
        if (bio_key == nullptr) {
            fprintf(stderr, "Failed to create memory BIO for private key.\n");
            return false;
        }

        // Load private key into EVP_PKEY structure
        EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio_key, nullptr, nullptr, nullptr);
        BIO_free(bio_key); // Free the BIO after reading the private key
        if (pkey == nullptr) {
            fprintf(stderr, "Failed to load private key from string.\n");
            return false;
        }

        // Convert EVP_PKEY to PEM format and write to buffer
        BIO* pem_bio_key = BIO_new(BIO_s_mem());
        if (PEM_write_bio_PrivateKey(pem_bio_key, pkey, nullptr, nullptr, 0, nullptr, nullptr) <= 0) {
            BIO_free_all(pem_bio_key);
            EVP_PKEY_free(pkey);
            fprintf(stderr, "Failed to convert private key to PEM format.\n");
            return false;
        }
        BIO_get_mem_ptr(pem_bio_key, &mem_ptr);
        BIO_set_close(pem_bio_key, BIO_NOCLOSE);
        BIO_free_all(pem_bio_key);

        // Add the private key to the context using a const_buffer
        if (!set_context_private_key(context, asio::buffer(mem_ptr->data, mem_ptr->length))) {
            OPENSSL_free(mem_ptr->data);
            EVP_PKEY_free(pkey);
            return false;
        }
        OPENSSL_free(mem_ptr->data);
        EVP_PKEY_free(pkey);

        return true;
    }

    // Helper function to set certificate in context without throwing exceptions
    static bool set_context_certificate(asio::ssl::context& context, const asio::const_buffer& buffer) {
        boost::system::error_code ec;
        context.use_certificate(buffer, asio::ssl::context::pem, ec);
        return !ec;
    }

    // Helper function to set private key in context without throwing exceptions
    static bool set_context_private_key(asio::ssl::context& context, const asio::const_buffer& buffer) {
        boost::system::error_code ec;
        context.use_private_key(buffer, asio::ssl::context::pem, ec);
        return !ec;
    }

    void connect(const std::shared_ptr<Session> &session) override {
      if(!session->connection->socket->lowest_layer().is_open()) {
        auto resolver = std::make_shared<asio::ip::tcp::resolver>(*io_service);
        async_resolve(*resolver, *host_port, [this, session, resolver](const error_code &ec, resolver_results results) {
          auto lock = session->connection->handler_runner->continue_lock();
          if(!lock)
            return;
          if(!ec) {
            session->connection->set_timeout(this->config.timeout_connect);
            asio::async_connect(session->connection->socket->lowest_layer(), results, [this, session, resolver](const error_code &ec, async_connect_endpoint /*endpoint*/) {
              session->connection->cancel_timeout();
              auto lock = session->connection->handler_runner->continue_lock();
              if(!lock)
                return;
              if(!ec) {
                asio::ip::tcp::no_delay option(true);
                error_code ec;
                session->connection->socket->lowest_layer().set_option(option, ec);

                if(!this->config.proxy_server.empty()) {
                  auto write_buffer = std::make_shared<asio::streambuf>();
                  std::ostream write_stream(write_buffer.get());
                  auto host_port = this->host + ':' + std::to_string(this->port);
                  write_stream << "CONNECT " + host_port + " HTTP/1.1\r\n"
                               << "Host: " << host_port << "\r\n\r\n";
                  session->connection->set_timeout(this->config.timeout_connect);
                  asio::async_write(session->connection->socket->next_layer(), *write_buffer, [this, session, write_buffer](const error_code &ec, std::size_t /*bytes_transferred*/) {
                    session->connection->cancel_timeout();
                    auto lock = session->connection->handler_runner->continue_lock();
                    if(!lock)
                      return;
                    if(!ec) {
                      std::shared_ptr<Response> response(new Response(this->config.max_response_streambuf_size, session->connection));
                      session->connection->set_timeout(this->config.timeout_connect);
                      asio::async_read_until(session->connection->socket->next_layer(), response->streambuf, "\r\n\r\n", [this, session, response](const error_code &ec, std::size_t /*bytes_transferred*/) {
                        session->connection->cancel_timeout();
                        auto lock = session->connection->handler_runner->continue_lock();
                        if(!lock)
                          return;
                        if(response->streambuf.size() == response->streambuf.max_size()) {
                          session->callback(make_error_code::make_error_code(errc::message_size));
                          return;
                        }

                        if(!ec) {
                          if(!ResponseMessage::parse(response->content, response->http_version, response->status_code, response->header))
                            session->callback(make_error_code::make_error_code(errc::protocol_error));
                          else {
                            if(response->status_code.compare(0, 3, "200") != 0)
                              session->callback(make_error_code::make_error_code(errc::permission_denied));
                            else
                              this->handshake(session);
                          }
                        }
                        else
                          session->callback(ec);
                      });
                    }
                    else
                      session->callback(ec);
                  });
                }
                else
                  this->handshake(session);
              }
              else
                session->callback(ec);
            });
          }
          else
            session->callback(ec);
        });
      }
      else
        write(session);
    }

    void handshake(const std::shared_ptr<Session> &session) {
      SSL_set_tlsext_host_name(session->connection->socket->native_handle(), this->host.c_str());

      session->connection->set_timeout(this->config.timeout_connect);
      session->connection->socket->async_handshake(asio::ssl::stream_base::client, [this, session](const error_code &ec) {
        session->connection->cancel_timeout();
        auto lock = session->connection->handler_runner->continue_lock();
        if(!lock)
          return;
        if(!ec)
          this->write(session);
        else
          session->callback(ec);
      });
    }
  };
} // namespace SimpleWeb

#endif /* SIMPLE_WEB_CLIENT_HTTPS_HPP */
