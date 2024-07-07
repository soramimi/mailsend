#ifndef PLATFORM_H
#define PLATFORM_H

#include <string>
#include <vector>

#ifdef _WIN32

void socket_startup();
void socket_cleanup();

#else

#define socket_startup()
#define socket_cleanup()

#endif

class MailSend {
public:
	struct Mail {
		std::string mail_from;
		std::string rcpt_to;
		std::string subject;
		std::vector<std::string> header;
		std::vector<std::string> body;
		void add_header(std::string const &s) { header.push_back(s); }
		void add_body(std::string const &s) { body.push_back(s); }
		Mail() = default;
		Mail(std::string_view const &text);
	};
private:
	struct Private;
	Private *m;

	struct HeaderLine {
		int order = 0;
		std::string name;
		std::string value;
	};

	bool read_line(std::string *out);
	int write(char const *p, int n = -1);
	void write_line(char const *p, int n = -1);
	void write_line(std::string const &s);
	static void make_header(Mail *mail, std::vector<HeaderLine> *out);
public:
	struct DateTime {
		int year = 0;
		int month = 0;
		int day = 0;
		int hour = 0;
		int minute = 0;
		int second = 0;
		int gmtoff = 0;
	};
	static void get_current_time(DateTime *out);
	static std::string get_current_date_string();
public:
	MailSend() = default;
	MailSend(const std::string &smtp_server, std::string const &helo);
	~MailSend();
	MailSend(MailSend const &) = delete;
	void operator = (MailSend const &) = delete;
	void set_smtp_server(std::string const &server);
	void set_helo(std::string const &domain);
	void send(Mail mail);
};

#endif // PLATFORM_H
