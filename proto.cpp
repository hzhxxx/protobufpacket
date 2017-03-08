

#include "addressbook.pb.h"
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/descriptor_database.h>
#include <google/protobuf/dynamic_message.h>

#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/type_resolver.h>
#include <google/protobuf/util/type_resolver_util.h>

#include <zlib.h>  // adler32,crc32
#include <bitset>
#include <string>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <thread>
#include <atomic>
#include <sstream>
#include <arpa/inet.h>  // htonl, ntohl
#include <stdint.h>


using namespace std::placeholders;
using namespace google;
using namespace std;

//g++ -g proto.cpp addressbook.pb.cc -lprotobuf -lz -std=gnu++11 -o proto -Wl,-rpath,/usr/local/lib -I/usr/local/include/
/*
zlib库
yum install zlib-devel
protolbuf,测试代码使用 v3.2版本
https://github.com/google/protobuf/
参考
https://github.com/chenshuo/recipes/blob/master/protobuf/codec.h
*/

/*
struct ProtobufTransportFormat __attribute__ ((__packed__))
{
    int32_t  len;
    int16_t  flag; 
    int16_t  nameLen;
    char     typeName[nameLen];
    char     protobufData[len - nameLen - 8];
    int32_t  checkSum;
}
1. flag 
从最低位开始,
第0位用作校验类型, 0: adler32(默认), 1:表示使用的是 CRC32。
第1位用来表示BufData的protobuf编码类型，0: 二进制编码(默认),1:json编码
第2位用来表示BufData的压缩类型，0:不压缩(默认)；1:zip压缩
2. checkSum
只支持adler32和crc32校验和,校验内容按顺序包括 flag+namelen+
typeName+protobufData
3. len 
不包括自身四字节长度
4. protobufData长度
(len - nameLen - 8)等于下面表达式
(len - NameLen - sizeof(Flag) - sizeof(NameLen) - sizeof(CheckSum))
5. 支持Json 打包和解包,类似如下格式
	{"Length":311,"Flag":2,"NameLen":21,"TypeName":
"tutorial.AddressBook","PB_Data":{Json数据},"CheckSum":-2121440958}
*/


//用于支持 TCP 流格式的数据包解析
class CProtobufPacket final
{
public:	
	inline const std::string encode(const protobuf::Message& message);
	inline protobuf::Message* decode(const std::string& buf);	
	inline void set_proto_checksum_algorithm(bool adler32);
	inline void set_proto_format(bool json);
	inline void set_proto_zip(bool zip);
	inline bool get_proto_checksum_algorithm();
	inline bool get_proto_format();
	inline bool get_proto_zip();
	inline int32_t get_packet_length();
	inline int16_t get_packet_flag();
	inline const std::string get_packet_typeName();
	inline int16_t get_packet_typeName_length();
	inline const std::string get_json_message();
	inline int32_t get_check_sum();
private:	
	const int32_t HEAD_LEN = sizeof(int32_t);
	const int16_t FLAG_LEN = sizeof(int16_t);
	const int16_t NAME_LEN = sizeof(int16_t);
	const int16_t CHECKSUM_LEN = sizeof(int32_t);
	const int16_t CHECKSUM_ALGORITHM_INDXE = 0;
	const int16_t PROTO_FORMAT_INDXE = 1;
	const int16_t PROTO_ZIP_INDXE = 2;
	const int32_t MIN_LENGTH = HEAD_LEN + FLAG_LEN + NAME_LEN + CHECKSUM_LEN;
	int32_t packet_length_ = 0;
	int16_t typeName_length_ = 0;
	int32_t check_sum_ = 0;
	std::string typeName_ = "";
	std::string json_message_ = "";
	std::bitset<16> flag_ = 0;	
	std::function<size_t (size_t,const Bytef*,size_t)> checksum_f = std::bind(::adler32,_1,_2,_3);
	protobuf::Message* createMessage(const std::string& type_name);
	inline int32_t asInt32(const char* buf);
	inline int16_t asInt16(const char* buf);
	bool unzip(std::string &json);
};

//用于支持 Json 流格式的数据包解析,比较适用于HTTP(HTTPS)做包头
class CJsonPacket final
{
public:	
	inline const std::string encode(const protobuf::Message& message);	
	inline protobuf::Message* decode(const std::string& json);
private:
	const std::string LENGTH = "\"Length\":";
	const std::string FLAG = "\"Flag\":";
	const std::string NAMELEN = "\"NameLen\":";
	const std::string TYPENAME = "\"TypeName\":";
	const std::string PB_DATA = "\"PB_Data\":";
	const std::string CHECKSUM = "\"CheckSum\":";
	inline const std::string get_value(
		const std::string &json,
		const std::string& key,size_t &start);
};


inline const std::string CProtobufPacket::encode(
	const protobuf::Message& message)
{
	std::string result = "";
	result.resize(HEAD_LEN);
	
	//设置 flag
	int16_t be16 = ::htons(flag_.to_ulong());
	result.append(reinterpret_cast<char*>(&be16),sizeof(int16_t));

	//设置 nameLen和typeName(\0结尾)
	typeName_ = message.GetTypeName();	
	typeName_length_ = static_cast<int16_t>(typeName_.size() + 1);
	be16 = ::htons(typeName_length_);
	result.append(reinterpret_cast<char*>(&be16),sizeof(int16_t));
	result.append(typeName_.c_str(), typeName_length_);
	bool success = true;
	//设置 protobufData
	if(flag_.test(PROTO_FORMAT_INDXE))
	{
		protobuf::util::JsonOptions options;
		//options.add_whitespace = true;		
		success = MessageToJsonString(message, &json_message_, options).ok();
		//std::cout<<"json:"<<json_message_<<endl;
		if(flag_.test(PROTO_ZIP_INDXE))
		{
			uint64_t ziplen = compressBound(json_message_.size());			
			std::shared_ptr<Bytef> zip(new Bytef[ziplen + 1]);
			int err = compress(zip.get(),&ziplen,
			(const Bytef*)json_message_.c_str(),json_message_.size());
			if(err != Z_OK)
			{
				success = false;
			}
			else
			{
				result.append((char*)zip.get(),ziplen);
			}			
		}
		else
		{
			result.append(json_message_);
			//std::cout<<"11 result json:"<<endl;
			//std::cout<<result<<endl;
		}
	}
	else
	{
		success = message.AppendToString(&result);		
	}
	
	if(!success)
	{
		result.clear();
		return result;
	}

	if(flag_.test(CHECKSUM_ALGORITHM_INDXE))
	{
		checksum_f = std::bind(crc32,_1,_2,_3);
	}

	//校验和
	 check_sum_ = checksum_f(1,
       reinterpret_cast<const Bytef*>(result.c_str() + HEAD_LEN), 
	   result.size() - HEAD_LEN);
	int32_t checkSum = ::htonl(check_sum_);
	result.append(reinterpret_cast<char*>(&checkSum), sizeof(int32_t));
		
	//网络序数据包长度
	packet_length_ = result.size() - HEAD_LEN;
	int32_t len = ::htonl(packet_length_);
	std::copy(reinterpret_cast<char*>(&len),
			reinterpret_cast<char*>(&len) + sizeof(len),
			result.begin());
	return result;	
}

inline protobuf::Message* 
CProtobufPacket::decode(const std::string& buf)
{
  int32_t total = static_cast<int32_t>(buf.size());
  int32_t len = asInt32(buf.c_str());
  if (total < MIN_LENGTH || len >= total)
  {
	  return nullptr;
  }
  
  //int32_t checkSum = asInt32(buf.c_str() + buf.size() - HEAD_LEN);
  int32_t checkSum = asInt32(buf.c_str() + len);
  flag_ = asInt16(buf.c_str() + HEAD_LEN);	
  if(flag_.test(CHECKSUM_ALGORITHM_INDXE))
  {
	  checksum_f = std::bind(crc32,_1,_2,_3);
  }
  
  int32_t compute_checkSum = checksum_f(1, 
  reinterpret_cast<const Bytef*>(buf.c_str() + HEAD_LEN), 
  len - CHECKSUM_LEN);
  if (checkSum != compute_checkSum)
  {
	  return nullptr;
  }
  
  int16_t nameLen = asInt16(buf.c_str() + HEAD_LEN + FLAG_LEN);
  if (nameLen < 2 || nameLen > len - 2*HEAD_LEN)
  {
	  return nullptr;
  }
  
  std::string typeName = buf.substr(HEAD_LEN + FLAG_LEN + NAME_LEN,nameLen);
  google::protobuf::Message* message = createMessage(typeName);
  if(!message)
  {
	  return nullptr;
  }
  const char* data = buf.c_str() + HEAD_LEN + FLAG_LEN + NAME_LEN + nameLen;
  int32_t dataLen = len - FLAG_LEN - NAME_LEN - CHECKSUM_LEN - nameLen;
  bool success = false;
  
  if(flag_.test(PROTO_FORMAT_INDXE))
  {
	  protobuf::util::JsonParseOptions options;
	  std::string json = buf.substr
	  (HEAD_LEN + FLAG_LEN + NAME_LEN + nameLen,dataLen);
	  if(flag_.test(PROTO_ZIP_INDXE))
	  {
		  success = unzip(json);
	  }
	  success = JsonStringToMessage(json,message, options).ok();
  }
  else
  {
	  success = message->ParseFromArray(data, dataLen);
  }

  if(!success)
  {	  
     delete message;	 
  }
  packet_length_ = total;

  return success ? message : nullptr;
}

inline google::protobuf::Message* CProtobufPacket::createMessage(const std::string& type_name)
{
  google::protobuf::Message* message = nullptr;
  const google::protobuf::Descriptor* descriptor =
    google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(type_name);
  if (descriptor)
  {
    const google::protobuf::Message* prototype =
      google::protobuf::MessageFactory::generated_factory()->GetPrototype(descriptor);
    if (prototype)
    {
      message = prototype->New();
    }
  }
  return message;
}

bool CProtobufPacket::unzip(std::string &json)
{
	//最大支持4倍压缩
	uint64_t unziplen = json.size() * 4;
	shared_ptr<Bytef> unzip(new Bytef[unziplen + 1]);		  
	int err = uncompress(unzip.get(),&unziplen,(const Bytef *)json.c_str(),json.size());
	if(err == Z_OK)
	{
		json.clear();
		json.resize(unziplen);
		std::copy((const char *)unzip.get(),
		(const char *)unzip.get() + unziplen,json.begin());		
	}	
	return err == Z_OK ? true : false;
}

inline int32_t CProtobufPacket::asInt32(const char* buf)
{
	//int32_t be32 = 0;
  	//::memcpy(&be32, buf, sizeof(be32));
	//return ::ntohl(be32);

	uint32_t ch1 = 0, ch2 = 0, ch3 = 0,ch4 = 0;
	ch1 = (uint32_t)(buf[0] & 0xff);
	ch2 = (uint32_t)(buf[1] & 0xff);
	ch3 = (uint32_t)(buf[2] & 0xff);
	ch4 = (uint32_t)(buf[3] & 0xff);
	return (ch1 << 24) + (ch2 << 16) + (ch3 << 8) + (ch4 << 0);	
}

inline int16_t CProtobufPacket::asInt16(const char* buf)
{
	//int16_t be16 = 0;
  //::memcpy(&be16, buf, sizeof(int16_t));
  //return ::ntohs(be16);

	uint16_t ch1 = 0, ch2 = 0;
	ch1 = (unsigned short)(buf[0] & 0xff);
	ch2 = (unsigned short)(buf[1] & 0xff);
	return (ch1 << 8) + (ch2 << 0); 
}

inline void CProtobufPacket::set_proto_checksum_algorithm(bool adler32)
{
	flag_[CHECKSUM_ALGORITHM_INDXE] = adler32 ? 0 : 1;
}

inline bool CProtobufPacket::get_proto_checksum_algorithm()
{
	return flag_.test(CHECKSUM_ALGORITHM_INDXE);
}

inline void CProtobufPacket::set_proto_format(bool json)
{
	flag_[PROTO_FORMAT_INDXE] = json ? 1 : 0;
}

inline bool CProtobufPacket::get_proto_format()
{
	return flag_.test(PROTO_FORMAT_INDXE);
}

inline void CProtobufPacket::set_proto_zip(bool zip)
{
	flag_[PROTO_ZIP_INDXE] = zip ? 1 : 0;
}

inline bool CProtobufPacket::get_proto_zip()
{
	return flag_.test(PROTO_ZIP_INDXE);
}

inline int32_t CProtobufPacket::get_packet_length()
{
	return packet_length_;
}

inline int16_t CProtobufPacket::get_packet_flag()
{
	return flag_.to_ulong();
}

inline const std::string CProtobufPacket::get_packet_typeName()
{
	return typeName_;
}

inline int16_t CProtobufPacket::get_packet_typeName_length()
{
	return typeName_length_;
}

inline const std::string CProtobufPacket::get_json_message()
{
	return json_message_;
}

inline int32_t CProtobufPacket::get_check_sum()
{
	return check_sum_;
}

inline const std::string CJsonPacket::encode(const protobuf::Message& message)
{		
	CProtobufPacket packet;
	packet.set_proto_format(true);
	packet.encode(message);	
	std::stringstream json;
	json<<"{"
	<<LENGTH<<packet.get_packet_length()<<","
	<<FLAG<<packet.get_packet_flag()<<","
	<<NAMELEN<<packet.get_packet_typeName_length()<<","
	<<TYPENAME<<"\""<<packet.get_packet_typeName()<<"\","
	<<PB_DATA<<packet.get_json_message()<<","
	<<CHECKSUM<<packet.get_check_sum()
	<<"}";
	return json.str();
}

inline protobuf::Message* CJsonPacket::decode(const std::string& json)
{
	std::string buff = "";
	
	int32_t be32 = 0;	
	size_t start = 0;
	be32 = htonl(std::stoi(get_value(json,LENGTH,start),nullptr,0));
	buff.append(reinterpret_cast<char*>(&be32),sizeof(int32_t));

	be32 = htons(std::stoi(get_value(json,FLAG,start),nullptr,0));
	buff.append(reinterpret_cast<char*>(&be32),sizeof(int16_t));

	int32_t namelen = std::stoi(get_value(json,NAMELEN,start),nullptr,0);
	be32 = htons(namelen);
	buff.append(reinterpret_cast<char*>(&be32),sizeof(int16_t));
	
	std::string s = get_value(json,TYPENAME,start);
	assert(s.size() > 2);
	s = s.substr(1,s.size() - 2);	
	buff.append(s.c_str(), namelen);

	start = json.find(PB_DATA,start);
	size_t end = json.rfind(CHECKSUM);
	if(start != std::string::npos &&
	end != std::string::npos)
	{
		start += PB_DATA.size();
		assert(end - start - 1 > 0);
		buff.append(json.substr(start,end - start - 1));
	}	
	
	start = end + CHECKSUM.size();
	end = json.rfind("}");
	if(end != std::string::npos)
	{
		s = json.substr(start,end - start);		
		be32 = htonl(std::stoi(s,nullptr,0));
		buff.append(reinterpret_cast<char*>(&be32),sizeof(int32_t));
	}
  	
	CProtobufPacket packet;
	return packet.decode(buff);
}

inline const std::string CJsonPacket::get_value(
	const std::string& json,const std::string& key,size_t &start)
{
	size_t pos = json.find(key,start);
	if(pos != std::string::npos)
	{
		pos += key.size();
		size_t end = json.find(",",pos);
		if(pos != std::string::npos)
		{
			start = end;
			assert(end > pos);
			return json.substr(pos,end - pos);
		}
	}	
	return "";
}

void addPerson(tutorial::AddressBook &address_book)
{
	tutorial::Person* person = address_book.add_people();
	assert(person);	
	person->set_id(100);
	*person->mutable_name() = "huang黄志辉";
	person->set_email("888888@qq.com");
	//
	tutorial::Person::PhoneNumber* phone_number = person->add_phones();
    phone_number->set_number("18912345678");
	phone_number->set_type(tutorial::Person::MOBILE);

	phone_number = person->add_phones();
    phone_number->set_number("075512345678");
	phone_number->set_type(tutorial::Person::WORK);	
}

void print_(const tutorial::AddressBook *book)
{
	for(int i = 0;i < book->people_size();++i)
	{
		const tutorial::Person &person = book->people(i);
		std::cout<<"id:"<<person.id()<<endl;
		std::cout<<"e-mail:"<<person.email()<<endl;
		std::cout<<"name:"<<person.name()<<endl;
		for(int k = 0;k< person.phones_size();++k)
		{
			const tutorial::Person_PhoneNumber &number = person.phones(k);
			std::cout<<"number:"<<number.number()<<endl;
			std::cout<<"number type:"<<number.type()<<endl;
		}
	}
}

int main(int argc,char * argv[])
{	
	GOOGLE_PROTOBUF_VERIFY_VERSION;
	
	tutorial::AddressBook address_book;

	addPerson(address_book);
	addPerson(address_book);
	//std::string protobuf = "";
	//assert(address_book.SerializeToString(&protobuf));

	CProtobufPacket packet;
	//packet.set_proto_checksum_algorithm(true);
	packet.set_proto_format(true);
	//packet.set_proto_zip(true);
	std::string buf = packet.encode(address_book);

	//std::cout<<"buf"<<buf<<endl;
	buf.append("dafdfdsa");
	tutorial::AddressBook *book = dynamic_cast<tutorial::AddressBook*>(packet.decode(buf));  
	print_(book);

	CJsonPacket jsonpacket;
	buf = jsonpacket.encode(address_book);
	std::cout<<buf.size()<<","<<buf<<endl;
	
	book = dynamic_cast<tutorial::AddressBook*>(jsonpacket.decode(buf));
	print_(book);		

	// Optional:  Delete all global objects allocated by libprotobuf.
  	google::protobuf::ShutdownProtobufLibrary();

	return 0;
}
