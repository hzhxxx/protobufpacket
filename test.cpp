
//g++ -g test.cpp proto.cpp addressbook.pb.cc -lprotobuf -lz -std=gnu++11 -o proto -Wl,-rpath,/usr/local/lib -I/usr/local/include/
//g++ -rdynamic -std=gnu++11 -fPIC -g test.cpp proto.cpp addressbook.pb.cc -I/usr/local/include/ -Wl,-rpath,/usr/local/lib -Wl,-Bdynamic -lz -Wl,-Bstatic -lprotobuf -Wl,-Bdynamic -o proto
//g++ -rdynamic -std=gnu++11 -fPIC -O3 test.cpp proto.cpp addressbook.pb.cc -I/usr/local/include/ -Wl,-rpath,/usr/local/lib -Wl,-Bdynamic -lz -Wl,-Bstatic -lprotobuf -Wl,-Bdynamic -o proto

#include "addressbook.pb.h"
#include "proto.h"
#include <iostream>
#include <sys/time.h>
using namespace std;

//以下部分为测试代码
void addPerson(protocol::AddressBook &address_book)
{
	protocol::Person* person = address_book.add_people();
	assert(person);
	static int32_t id = 100;	
	person->set_id(id++);
	*person->mutable_name() = "huang马克黄";
	person->set_email("888888@qq.com");
	//
	protocol::Person::PhoneNumber* phone_number = person->add_phones();
    phone_number->set_number("18912345678");
	phone_number->set_type(protocol::Person::MOBILE);

	phone_number = person->add_phones();
    phone_number->set_number("075512345678");
	phone_number->set_type(protocol::Person::WORK);	
}

void print_(const protocol::AddressBook *book)
{
	for(int i = 0;i < book->people_size();++i)
	{
		const protocol::Person &person = book->people(i);
		std::cout<<"id:"<<person.id()<<endl;
		std::cout<<"e-mail:"<<person.email()<<endl;
		std::cout<<"name:"<<person.name()<<endl;
		for(int k = 0;k< person.phones_size();++k)
		{
			const protocol::Person_PhoneNumber &number = person.phones(k);
			std::cout<<"number:"<<number.number()<<endl;
			std::cout<<"number type:"<<number.type()<<endl;
		}
	}
}

int main(int argc,char * argv[])
{
	GOOGLE_PROTOBUF_VERIFY_VERSION;
	
	struct timeval tv1,tv2,tv_start;
	gettimeofday(&tv1,0);
	tv_start = tv1;
	protocol::AddressBook address_book;
	for(int i = 0;i< 6000;++i)
	{
		addPerson(address_book);
		//addPerson(address_book);
	}
	//std::string protobuf = "";
	//assert(address_book.SerializeToString(&protobuf));

	CProtobufPacket packet;
	packet.set_proto_checksum_algorithm(true);
	packet.set_proto_format(true);
	packet.set_proto_zip(true);
	std::string buf = packet.encode(address_book);
	gettimeofday(&tv2,0);
	int times = 0;
	times = ((tv2.tv_sec  - tv1.tv_sec) * 1000000 + (tv2.tv_usec - tv1.tv_usec))/1000;
	std::cout<<"encode time:"<<times<<endl;

	//std::cout<<"buf"<<buf<<endl;
	buf.append("附加测试,判定只解析需要的数据");
	protocol::AddressBook *book = dynamic_cast<protocol::AddressBook*>(packet.decode(buf));
	gettimeofday(&tv1,0);
	times = ((tv1.tv_sec  - tv2.tv_sec) * 1000000 + (tv1.tv_usec - tv2.tv_usec))/1000;
	std::cout<<"decode time:"<<times<<endl;
	//print_(book);

	CJsonPacket jsonpacket;
	buf = jsonpacket.encode(*book);
	gettimeofday(&tv2,0);
	times = ((tv2.tv_sec  - tv1.tv_sec) * 1000000 + (tv2.tv_usec - tv1.tv_usec))/1000;
	std::cout<<"json encode time:"<<times<<endl;
    delete book;
	std::cout<<buf.size()<<endl;
	//std::cout<<buf<<endl;
	
	book = dynamic_cast<protocol::AddressBook*>(jsonpacket.decode(buf));
	gettimeofday(&tv1,0);
	times = ((tv1.tv_sec  - tv2.tv_sec) * 1000000 + (tv1.tv_usec - tv2.tv_usec))/1000;
	std::cout<<"json decode time:"<<times<<endl;

	//print_(book);	
	delete book;	

	// Optional:  Delete all global objects allocated by libprotobuf.
  	google::protobuf::ShutdownProtobufLibrary();

	times = ((tv1.tv_sec  - tv_start.tv_sec) * 1000000 + (tv1.tv_usec - tv_start.tv_usec))/1000;
	std::cout<<"all time:"<<times<<endl;

	return 0;
}
