
#include "MailSend.h"

int main(int argc, char **argv)
{
	socket_startup();

	std::string mail_from = "foo@example.com";
	std::string rcpt_to = "soramimi@soramimi.jp";
	std::string date = MailSend::get_current_date_string();
	std::string subject = "test";

	MailSend::Mail mail;
	mail.mail_from = mail_from;
	mail.rcpt_to = rcpt_to;

	mail.header.push_back("From: " + mail_from);
	mail.header.push_back("To: " + rcpt_to);
	mail.header.push_back("Date: " + date);
	mail.header.push_back("Subject: " + subject);

	mail.lines.push_back("Hello, world 1");
	mail.lines.push_back("Hello, world 2");
	mail.lines.push_back("Hello, world 3");

	MailSend ms;
	ms.send(mail);

	socket_cleanup();
	return 0;
}



