
#include "MailSend.h"
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>
#include <climits>

#ifdef _WIN32

#include <WinSock2.h>
#include <Windows.h>

void socket_startup()
{
	WSAData wsaData;
	WSAStartup(MAKEWORD(2,0), &wsaData);
}

void socket_cleanup()
{
	WSACleanup();
}

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


MailSend::Mail::Mail(const std::string_view &text)
{
	enum State {
		START,
		HEADER,
		BODY,
	};
	State state = START;
	char const *begin = text.data();
	char const *end = begin + text.size();
	char const *next = begin;
	char const *line = next;
	while (next < end) {
		int c = 0;
		if (next < end) {
			c = (unsigned char)*next;
		}
		if (c == '\n' || c == '\r') {
			std::string_view sv(line, next - line);
			if (state == START && sv.empty()) {
				// ignore
			} else {
				if (state == START) {
					state = HEADER;
				}
				if (state == HEADER) {
					if (sv.empty()) {
						state = BODY;
					} else {
						header.push_back(std::string(sv));
					}
				} else if (state == BODY) {
					body.push_back(std::string(sv));
				}
			}
			if (c == 0) break;
			if (c == '\r') next++;
			if (c == '\n') next++;
			line = next;
		} else {
			next++;
		}
	}
}


struct MailSend::Private {
	int sock;
	bool disconnected = false;
	std::mutex recv_mutex;
	std::vector<char> recv_buffer;
	std::thread recv_thread;
	bool recv_thread_interrupted = {false};
	std::string smtp_server;
	std::string helo;
};

MailSend::MailSend(std::string const &smtp_server, const std::string &helo)
	: m(new Private)
{
	set_smtp_server(smtp_server);
}

void MailSend::set_smtp_server(const std::string &server)
{
	m->smtp_server= server;
}

void MailSend::set_helo(const std::string &domain)
{
	m->helo = domain;
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
	int z_min = dt.gmtoff;
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

static std::string trim(std::string const &line)
{
	size_t i = 0;
	size_t j = line.size();
	char const *p = line.c_str();
	while (i < j && isspace((unsigned char)p[i])) i++;
	while (i < j && isspace((unsigned char)p[j - 1])) j--;
	return line.substr(i, j - i);
}

void MailSend::make_header(Mail *mail, std::vector<HeaderLine> *out)
{
	out->clear();
	bool has_from = false;
	bool has_to = false;
	bool has_date = false;
	enum  {
		HEADER_ORDER_From = -9,
		HEADER_ORDER_To = -8,
		HEADER_ORDER_Date = -7,
		HEADER_ORDER_Subject = -6,
	};
	for (std::string const &line : mail->header) {
		HeaderLine h;
		h.order = UINT_MAX;
		auto i = line.find(':');
		if (i != std::string::npos) {
			h.name = trim(line.substr(0, i));
			h.value = trim(line.substr(i + 1));
			if (h.name == "From") {
				has_from = true;
				if (mail->mail_from.empty()) {
					mail->mail_from = h.value;
				}
				h.order = HEADER_ORDER_From;
			} else if (h.name == "To") {
				has_to = true;
				if (mail->rcpt_to.empty()) {
					mail->rcpt_to = h.value;
				}
				h.order = HEADER_ORDER_To;
			} else if (h.name == "Date") {
				has_date = true;
				h.order = HEADER_ORDER_Date;
			} else if (h.name == "Subject") {
				if (mail->subject.empty()) {
					mail->subject = h.value;
				}
				continue; // don't add subject here
			}
			out->push_back(h);
		}
	}
	if (!has_from) {
		HeaderLine h;
		h.name = "From";
		h.value = mail->mail_from;
		h.order = HEADER_ORDER_From;
		out->push_back(h);
	}
	if (!has_to) {
		HeaderLine h;
		h.name = "To";
		h.value = mail->rcpt_to;
		h.order = HEADER_ORDER_To;
		out->push_back(h);
	}
	if (!has_date) {
		HeaderLine h;
		h.name = "Date";
		h.value = get_current_date_string();
		h.order = HEADER_ORDER_Date;
		out->push_back(h);
	}
	{ // add subject here
		HeaderLine h;
		h.name = "Subject";
		h.value = mail->subject;
		h.order = HEADER_ORDER_Subject;
		out->push_back(h);
	}
	std::sort(out->begin(), out->end(), [](HeaderLine const &l, HeaderLine const &r){
		return [](HeaderLine const &l, HeaderLine const &r){
			if (l.order < r.order) return -1;
			if (l.order > r.order) return 1;
			return 0;
		}(l, r) < 0;
	});
}

void MailSend::send(Mail mail)
{
	std::vector<HeaderLine> header;
	make_header(&mail, &header);

	struct sockaddr_in server;

	m->sock = socket(AF_INET, SOCK_STREAM, 0);

	server.sin_family = AF_INET;
	server.sin_port = htons(25);
	
	{
		struct hostent *host = HostNameResolver().gethostbyname(m->smtp_server.c_str());
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
						write_line("HELO " + (m->helo.empty() ? std::string("exmaple.com") : m->helo));
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
						for (auto const &h : header) {
							write_line(h.name + ": " + h.value);
						}
						write_line("");
						for (std::string line : mail.body) {
							if (line.c_str()[0] == '.') {
								line = '.' + line;
							}
							write_line(line);
						}
						write_line(".");
						write_line("QUIT");
						state = State::QUIT;
					}
				} else {
					fprintf(stderr, "%s\n", line.c_str());
					break;
				}
			} else {
				std::this_thread::yield();
			}
		}

		m->recv_thread.join();
	}

	closesocket(m->sock);
}

