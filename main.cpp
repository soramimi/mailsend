
#include <arpa/inet.h>
#include <mutex>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

class MailSend {
private:
	int sock;
	bool disconnected = false;
	std::mutex recv_mutex;
	std::vector<char> recv_buffer;
	std::thread recv_thread;
	bool recv_thread_interrupted = {false};
	bool read_line(std::string *out)
	{
		out->clear();
		std::lock_guard<std::mutex> lock(recv_mutex);
		size_t n = recv_buffer.size();
		if (n > 0) {
			char const *p = recv_buffer.data();
			for (int i = 0; i < n; i++) {
				if (p[i] == '\n') {
					n = i + 1;
					if (i > 0 && p[i - 1] == '\r') i--;
					out->assign(p, i);
					auto it = recv_buffer.begin();
					recv_buffer.erase(it, it + n);
					return true;
				}
			}
		}
		return false;
	}
	void write_line(char const *p)
	{
		int n = strlen(p);
		send(sock, p, n, 0);
		send(sock, "\r\n", 2, 0);

	}
	void write_line(std::string const &s)
	{
		write_line(s.c_str());
	}
public:
	static std::string get_current_date()
	{
		time_t t;
		time(&t);
		struct tm *tm = localtime(&t);

		char mon[4];
		memcpy(mon, "JanFebMarAprMayJunJulAugSepOctNovDec" + (tm->tm_mon % 12 * 3), 3);
		mon[3] = 0;
		char tmp[100];
		char z_sign = '+';
		auto z_min = tm->tm_gmtoff;
		if (z_min < 0) {
			z_sign = '-';
			z_min = -z_min;
		}
		z_min /= 60;
		auto z_hour = z_min / 60;
		z_min %= 60;
		sprintf(tmp, "%d %s %d %02d:%02d:%02d %c%02d%02d"
				, tm->tm_mday
				, mon
				, tm->tm_year + 1900
				, tm->tm_hour
				, tm->tm_min
				, tm->tm_sec
				, z_sign
				, z_hour
				, z_min
				);
		return tmp;
	}
public:
	void run()
	{
		struct sockaddr_in server;
		char *deststr;

		deststr = "10.10.10.10"; // SMTP server

		sock = socket(AF_INET, SOCK_STREAM, 0);

		server.sin_family = AF_INET;
		server.sin_port = htons(25);

		server.sin_addr.s_addr = inet_addr(deststr);
		if (server.sin_addr.s_addr == 0xffffffff) {
			struct hostent *host;

			host = gethostbyname(deststr);
			if (host == nullptr) {
				return;
			}
			server.sin_addr.s_addr = *(unsigned int *)host->h_addr_list[0];
		}

		if (connect(sock, (struct sockaddr *)&server, sizeof(server)) == 0) {

			recv_thread = std::thread([&](){
				while (1) {
					char buf[1024];
					int n = recv(sock, buf, sizeof(buf), 0);
					if (n < 0) {
						disconnected = true;
						break;
					}
					if (recv_thread_interrupted) break;
					if (n > 0) {
						std::lock_guard<std::mutex> lock(recv_mutex);
						recv_buffer.insert(recv_buffer.end(), buf, buf + n);
					}
				}
			});

			enum class State {
				CONNECT,
				HELO,
				MAIL_FROM,
				RCPT_TO,
				DATA,
				QUIT,
			};
			State state = State::CONNECT;

			while (1) {
				if (disconnected) {
					break;
				}
				std::string line;
				if (read_line(&line)) {
					puts(line.c_str());
					int code = strtol(line.c_str(), nullptr, 10);
					if (code == 220) {
						if (state == State::CONNECT) {
							write_line("HELO example");
							state = State::HELO;
						}
					} else if (code == 221) {
						recv_thread_interrupted = true;
						break;
					} else if (code == 250) {
						if (state == State::HELO) {
							write_line("MAIL FROM: foo@example.com");
							state = State::MAIL_FROM;
						} else if (state == State::MAIL_FROM) {
							write_line("RCPT TO: bar@example.com");
							state = State::RCPT_TO;
						} else if (state == State::RCPT_TO) {
							write_line("DATA");
							state = State::DATA;
						}
					} else if (code == 354) {
						if (state == State::DATA) {
							std::string date = get_current_date();
							write_line("From: foo@example.com");
							write_line("To: bar@example.com");
							write_line("Date: " + date);
							write_line("Subject: test");
							write_line("");
							write_line("Hello, world 1");
							write_line("Hello, world 2");
							write_line("Hello, world 3");
							write_line(".");
							write_line("QUIT");
							state = State::QUIT;
						}
					}
				} else {
					std::this_thread::yield();
				}
			}
		}

		recv_thread.join();
		close(sock);
	}
};

int main(int argc, char **argv)
{
	MailSend ms;
	ms.run();
	return 0;
}



