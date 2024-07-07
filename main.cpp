
#include "MailSend.h"

int main(int argc, char **argv)
{
	socket_startup();

#if 0
	MailSend::Mail mail;
	mail.mail_from = "soramimi@soramimi.jp";
	mail.rcpt_to= "fi7s-fct@asahi-net.or.jp";
	mail.subject = "test9";

	mail.add_header("Subject: " + mail.subject);

	mail.add_body("Hello, world 1");
	mail.add_body("Hello, world 2");
	mail.add_body("Hello, world 3");
#else
	std::string text = R"---(
From: soramimi@soramimi.jp
To: fi7s-fct@asahi-net.or.jp
Subject: test10

Hello, world 1
Hello, world 2
Hello, world 3
)---";
	MailSend::Mail mail(text);

#endif

	MailSend ms("192.168.0.25", "example.com");
	ms.send(mail);

	socket_cleanup();
	return 0;
}



