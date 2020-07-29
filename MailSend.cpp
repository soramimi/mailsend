
#include "MailSend.h"
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32

#include <WinSock2.h>
#include <Windows.h>

void socket_startup();
void socket_cleanup();

#else

#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define socket_startup()
#define socket_cleanup()
#define closesocket(S) close(S)

#endif

#ifdef _WIN32

void socket_startup()
{
	WSAData wsaData;
	WSAStartup(MAKEWORD(2,0), &wsaData);
}

void socket_cleanup()
{
	WSACleanup();
}

#endif

struct MailSend::Private {
	int sock;
	bool disconnected = false;
	std::mutex recv_mutex;
	std::vector<char> recv_buffer;
	std::thread recv_thread;
	bool recv_thread_interrupted = {false};
};

MailSend::MailSend()
	: m(new Private)
{

}

MailSend::~MailSend()
{
	delete m;
}


bool MailSend::read_line(std::string *out)
{
	out->clear();
	std::lock_guard<std::mutex> lock(m->recv_mutex);
	size_t n = m->recv_buffer.size();
	if (n > 0) {
		char const *p = m->recv_buffer.data();
		for (size_t i = 0; i < n; i++) {
			if (p[i] == '\n') {
				n = i + 1;
				if (i > 0 && p[i - 1] == '\r') i--;
				out->assign(p, i);
				auto it = m->recv_buffer.begin();
				m->recv_buffer.erase(it, it + n);
				return true;
			}
		}
	}
	return false;
}

int MailSend::write(const char *p, int n)
{
	if (n == -1) {
		n = strlen(p);
	}
	return ::send(m->sock, p, n, 0);
}

void MailSend::write_line(const char *p, int n)
{
	write(p, n);
	write("\r\n", 2);
}

void MailSend::write_line(const std::string &s)
{
	write_line(s.c_str(), s.size());
}

void MailSend::get_current_time(MailSend::DateTime *out)
{
	*out = {};
#if _WIN32
	SYSTEMTIME t;
	TIME_ZONE_INFORMATION z;
	GetLocalTime(&t);
	GetTimeZoneInformation(&z);

	out->year = t.wYear;
	out->month = t.wMonth;
	out->day = t.wDay;
	out->hour = t.wHour;
	out->minute = t.wMinute;
	out->second = t.wSecond;
	out->gmtoff = -z.Bias * 60;
#else
	time_t t;
	time(&t);
	struct tm *tm = localtime(&t);

	out->year = tm->tm_year + 1900;
	out->month = tm->tm_mon + 1;
	out->day = tm->tm_mday;
	out->hour = tm->tm_hour;
	out->minute = tm->tm_min;
	out->second = tm->tm_sec;
	out->gmtoff = tm->tm_gmtoff;
#endif
}

std::string MailSend::get_current_date_string()
{
	DateTime dt;
	get_current_time(&dt);

	char mon[4];
	memcpy(mon, "JanFebMarAprMayJunJulAugSepOctNovDec" + ((dt.month + 11) % 12 * 3), 3);
	mon[3] = 0;

	char z_sign = '+';
	int z_min = 0;
	z_min = dt.gmtoff;
	if (z_min < 0) {
		z_sign = '-';
		z_min = -z_min;
	}
	z_min /= 60;
	auto z_hour = z_min / 60;
	z_min %= 60;

	char tmp[100];
	sprintf(tmp, "%d %s %d %02d:%02d:%02d %c%02d%02d"
			, dt.day
			, mon
			, dt.year
			, dt.hour
			, dt.minute
			, dt.second
			, z_sign
			, z_hour
			, z_min
			);
	return tmp;
}

void MailSend::send(const MailSend::Mail &mail)
{
	struct sockaddr_in server;
	char const *deststr;

	deststr = "10.10.10.10"; // SMTP server

	m->sock = socket(AF_INET, SOCK_STREAM, 0);

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

	if (connect(m->sock, (struct sockaddr *)&server, sizeof(server)) == 0) {

		m->recv_thread = std::thread([&](){
			while (1) {
				char buf[1024];
				int n = recv(m->sock, buf, sizeof(buf), 0);
				if (n < 0) {
					m->disconnected = true;
					break;
				}
				if (m->recv_thread_interrupted) break;
				if (n > 0) {
					std::lock_guard<std::mutex> lock(m->recv_mutex);
					m->recv_buffer.insert(m->recv_buffer.end(), buf, buf + n);
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
			if (m->disconnected) {
				break;
			}
			std::string line;
			if (read_line(&line)) {
				int code = strtol(line.c_str(), nullptr, 10);
				if (code == 220) {
					if (state == State::CONNECT) {
						write_line("HELO example");
						state = State::HELO;
					}
				} else if (code == 221) {
					m->recv_thread_interrupted = true;
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
						for (std::string const &line : mail.header) {
							write_line(line);
						}
						write_line("");
						for (std::string line : mail.lines) {
							if (line.c_str()[0] == '.') {
								line = '.' + line;
							}
							write_line(line);
						}
						write_line(".");
						write_line("QUIT");
						state = State::QUIT;
					}
				}
			} else {
				std::this_thread::yield();
			}
		}

		m->recv_thread.join();
	}

	closesocket(m->sock);
}
