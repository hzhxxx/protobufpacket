
//g++ -g test.cpp proto.cpp addressbook.pb.cc -lprotobuf -lz -std=gnu++11 -o proto -Wl,-rpath,/usr/local/lib -I/usr/local/include/

#include "addressbook.pb.h"
#include "proto.h"
#include <iostream>
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
	
	protocol::AddressBook address_book;

	for(int i = 0;i< 10;++i)
	{
		addPerson(address_book);
		//addPerson(address_book);
	}
	//std::string protobuf = "";
	//assert(address_book.SerializeToString(&protobuf));

	CProtobufPacket packet;
	//packet.set_proto_checksum_algorithm(true);
	packet.set_proto_format(true);
	//packet.set_proto_zip(true);
	std::string buf = packet.encode(address_book);

	//std::cout<<"buf"<<buf<<endl;
	buf.append("附加测试,判定只解析需要的数据");
	protocol::AddressBook *book = dynamic_cast<protocol::AddressBook*>(packet.decode(buf));  
	print_(book);

	CJsonPacket jsonpacket;
	buf = jsonpacket.encode(address_book);
	std::cout<<buf.size()<<","<<buf<<endl;
	
	book = dynamic_cast<protocol::AddressBook*>(jsonpacket.decode(buf));
	print_(book);		

	// Optional:  Delete all global objects allocated by libprotobuf.
  	google::protobuf::ShutdownProtobufLibrary();

	return 0;
}
