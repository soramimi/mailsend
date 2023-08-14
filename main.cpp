
#include "platform.h"
#include <mutex>
#include <thread>
#include <vector>
#include <string>

class HostNameResolver {
private:
	char buf[2048];
	struct hostent tmp;
public:
	struct hostent *gethostbyname(char const *name)
	{
		struct hostent *he = nullptr;
		{
			int err = 0;
#if defined(_WIN32) || defined(__APPLE__)
			he = ::gethostbyname(name);
#else
			gethostbyname_r(name, &tmp, buf, sizeof(buf), &he, &err);
#endif
		}
		return he;
	}
};

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
	void write_line(char const *p, size_t n = -1)
	{
		if (n == -1) {
			n = strlen(p);
		}
		send(sock, p, n, 0);
		send(sock, "\r\n", 2, 0);
	}
	void write_line(std::string const &s)
	{
		write_line(s.c_str(), s.size());
	}
public:
	static std::string get_current_date_string()
	{
#if _WIN32
		SYSTEMTIME t;
		TIME_ZONE_INFORMATION z;
		GetLocalTime(&t);
		GetTimeZoneInformation(&z);

		char mon[4];
		memcpy(mon, "JanFebMarAprMayJunJulAugSepOctNovDec" + ((t.wMonth + 11) % 12 * 3), 3);
		mon[3] = 0;
		char tmp[100];
		char z_sign = '+';
		int z_min = 0;
		z_min = -z.Bias;
		if (z_min < 0) {
			z_sign = '-';
			z_min = -z_min;
		}
		auto z_hour = z_min / 60;
		z_min %= 60;
		sprintf(tmp, "%d %s %d %02d:%02d:%02d %c%02d%02d"
				, t.wDay
				, mon
				, t.wYear
				, t.wHour
				, t.wMinute
				, t.wSecond
				, z_sign
				, z_hour
				, z_min
				);
		return tmp;

#else
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
#endif
	}
public:
	void run()
	{
		struct sockaddr_in server;
		char const *deststr;

		deststr = "10.0.0.10"; // SMTP server

		sock = socket(AF_INET, SOCK_STREAM, 0);

		server.sin_family = AF_INET;
		server.sin_port = htons(25);

		{
			struct hostent *host = HostNameResolver().gethostbyname(deststr);
			if (host == nullptr) {
				return;
			}
			server.sin_addr.s_addr = *(unsigned int *)host->h_addr_list[0];
		}


		struct Mail {
			std::string helo;
			std::string mail_from;
			std::string rcpt_to;

			std::string from;
			std::string to;
			std::string date;
			std::string subject;

			std::vector<std::string> body;
		};

		Mail mail;

		mail.from = "soramimi@soramimi.jp";
		mail.to = "soramimi@soramimi.jp";
		mail.subject = "test";

		mail.body.push_back("Hello, world 1");
		mail.body.push_back(".Hello, world 2");
		mail.body.push_back("..Hello, world 3");
		mail.body.push_back(".");

		if (connect(sock, (struct sockaddr *)&server, sizeof(server)) == 0) {

			if (mail.helo.empty()) {
				mail.helo = "example.com";
			}

			if (mail.mail_from.empty()) {
				mail.mail_from = mail.from;
			} else if (mail.from.empty()) {
				mail.from = mail.mail_from;
			}

			if (mail.rcpt_to.empty()) {
				mail.rcpt_to = mail.to;
			} else if (mail.to.empty()) {
				mail.to = mail.rcpt_to;
			}

			if (mail.date.empty()) {
				mail.date = get_current_date_string();
			}

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
					int code = strtol(line.c_str(), nullptr, 10);
					if (code == 220) {
						if (state == State::CONNECT) {
							write_line("HELO " + mail.helo);
							state = State::HELO;
						}
					} else if (code == 221) {
						recv_thread_interrupted = true;
						break;
					} else if (code == 250) {
						if (state == State::HELO) {
							write_line("MAIL FROM: " + mail.mail_from);
							state = State::MAIL_FROM;
						} else if (state == State::MAIL_FROM) {
							write_line("RCPT TO: " + mail.rcpt_to);
							state = State::RCPT_TO;
						} else if (state == State::RCPT_TO) {
							write_line("DATA");
							state = State::DATA;
						}
					} else if (code == 354) {
						if (state == State::DATA) {
							// send header
							write_line("From: " + mail.from);
							write_line("To: " + mail.to);
							write_line("Date: " + mail.date);
							write_line("Subject: " + mail.subject);
							write_line("");
							// send body
							for (std::string const &line : mail.body) {
								std::vector<char> v;
								v.reserve(line.size());
								char const *p = line.c_str();
								if (*p == '.') {
									v.push_back('.');
								}
								for (size_t i = 0; i < line.size(); i++) {
									char c = p[i];
									if (c == '\r') continue;
									if (c == '\n') continue;
									v.push_back(c);
								}
								if (v.empty()) {
									char c = 0;
									write_line(&c, 0);
								} else {
									write_line(v.data(), v.size());
								}
							}
							// end body
							write_line(".");
							write_line("QUIT");
							state = State::QUIT;
						}
					}
				} else {
					std::this_thread::yield();
				}
			}

			recv_thread.join();
		}

		closesocket(sock);
	}
};

int main(int argc, char **argv)
{
#ifdef _WIN32
	WSAData wsaData;
	WSAStartup(MAKEWORD(2,0), &wsaData);
#endif

	MailSend ms;
	ms.run();

#ifdef _WIN32
	WSACleanup();
#endif
	return 0;
}



