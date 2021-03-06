დამატებით ბიბლიოთეკად გამოყენებული გვაქვს მხოლოდ pthread.
ბევრი შეერთების მიღების პრობლემა გადავწყვიტეთ worker სრედებით
და epoll-ის გამოყენებით. მეინში იქმნება ლისენერ სრედები.
თითოეული იღებს ვირტუალური ჰოსტის შესაბამის პორტს და აიპის
და უსმენს შესაბამისად შექმნილი TCP სოკეტით. პარალელურად
გაშვებულია 1024 მუშა სრედი, რომლებიც ცდილობენ რომ epoll-იდან
მიიღონ ერთი ცალი ფაილ დესკრიპტორი, რომელსაც მოემსახურებიან.
epoll-ის ONESHOT და ის ფაქტი, რომ ერთხელ ამოღებულ ფაილ დესკრიპტორს
სრედი ბოლომდე ემსახურება და იპოლში არ აბრუნებს, გარანტიას იძლევიან
რომ არანაირი race condition ან რაიმე სხვა კონკურენტულობის პრობლემა არ
შეიქმნება სერვერის მუშაობის დროს.
კონფიგურაციის სტრუქტურა
აღწერს ვირტუალურ ჰოსტს.
ეს არის ერთი ბლოკი config ფაილში ჩაწერილი 
ინფორმაციისა.
struct config {
	char *vhost;
	char *document_root;
	char *cgi_bin;
	char *ip;
	char *port;
	char *log;
};


Keep-Alive ჰედერის პრობლემა გადავწყვიტეთ setsockopt-ის 
SO_RCVTIMEO ფლაგით. თუ კლიენტი  გვაძლევს ამ ჰედერს, ჩვენ 
ვსეტავთ ამ ფლაგს.
ჰეშირება სტანდარტული გვაქვს, ისეთი როგორიც პარადიგმებში,
ალგორითმებში და გაძლიერებულ ალგორითმებში ვისწავლეთ. ანუ 
მგორავ ჰეშს ვიყენებთ.
cgi - სკრიპტის გასაშვებად fork-ს ვაკეთებთ და child process
ვუშვებთ სკრიპტს. სკრიპტის გაშვებამდე ხდება გარემოს(environment)
დასეტვა და stdin და stdout-ის გადამისამართება socketfd-ზე
dup2-ის მეშვეობით. შემდეგ execve-თი გადახტება სკრიპტის კოდის ნაწილზე.
შემდეგ მშობელი პროცესი დავეითდება შვილობილზე სადაც სკრიპტია გაშვებული.
ლოგირებისთვის გაკეთებულია ორივე ტიპის ლოგის(ერორის და access-ის) სტრუქტურა.
მათი ინიციალიზება ხდება რექვესტის დამუშავების დროს. გვიწერია ფუნქცია 
რომელსაც გადაეცემა ეს სტრუქტურა და ბეჭდავს მოთხოვნის შესაბამის ფორმატში.
სტრუქტურა რომელიც ინახავს შეერთების დროს და IP მისამართს.
struct connect_time_and_ip {
	char *connect_time;
	char *Ip_address;
};

სტრუქტურა აქსესის პარამეტრებისთვის.
struct accesslog_params {
	struct connect_time_and_ip time_ip;//ზემოთ აღწერილი სტრუქტურა
	char *domain; //დომეინი
	char *requested_filename; //თუ ფაილზე იყო მოთხოვნა
	int sent_status_code; //მაგ 404
	int num_of_bytes_sent; 
	char *user_provided_info;
	char *error_msg;//თუ იყო ერორი
};

ჰედერების პროცესინგს ცხადია ბევრი ვარიანტის განხილვა ჭირდება. ამას დამატებული
cgi და დიდი სტრაქტი მივიღეთ

struct header_info {
	enum http_method method;//GET, POST
	enum request_type cgi_or_file; //რას გვთხოვენ
	char *requested_objname;
	char *ext;
	char *host;
	char *etag;
	bool keep_alive;
    /* for cgi */
    char *content_type;
    char *content_length;
    char *path_info;
    char *query_string;
    /***********/
	struct range_info *range;
};



