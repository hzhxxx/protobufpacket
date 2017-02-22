

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
*/


class CProtobufPacket final
{
public:	
	inline std::string encode(const protobuf::Message& message);
	inline protobuf::Message* decode(const std::string& buf);	
	inline void set_proto_checksum_algorithm(bool adler32);
	inline void set_proto_format(bool json);
	inline void set_proto_zip(bool zip);
	inline bool get_proto_checksum_algorithm();
	inline bool get_proto_format();
	inline bool get_proto_zip();
private:	
	const int32_t HEAD_LEN = sizeof(int32_t);
	const int16_t FLAG_LEN = sizeof(int16_t);
	const int16_t NAME_LEN = sizeof(int16_t);
	const int16_t CHECKSUM_LEN = sizeof(int32_t);
	const int16_t CHECKSUM_ALGORITHM_INDXE = 0;
	const int16_t PROTO_FORMAT_INDXE = 1;
	const int16_t PROTO_ZIP_INDXE = 2;
	const int32_t MIN_LENGTH = HEAD_LEN + FLAG_LEN + NAME_LEN + CHECKSUM_LEN;
	std::bitset<16> flag_ = 0;
	std::function<size_t (size_t,const Bytef*,size_t)> checksum_f = std::bind(::adler32,_1,_2,_3);

	protobuf::Message* createMessage(const std::string& type_name);
	inline int32_t asInt32(const char* buf);
	inline int16_t asInt16(const char* buf);
	bool unzip(std::string &json);
};


inline std::string CProtobufPacket::encode(
	const protobuf::Message& message)
{
	std::string result = "";
	result.resize(HEAD_LEN);
	
	//设置 flag
	int16_t be16 = ::htons(flag_.to_ulong());
	result.append(reinterpret_cast<char*>(&be16),sizeof(int16_t));

	//设置 nameLen和typeName(\0结尾)
	const std::string& typeName = message.GetTypeName();	
	int16_t nameLen = static_cast<int16_t>(typeName.size() + 1);
	be16 = ::htons(nameLen);
	result.append(reinterpret_cast<char*>(&be16),sizeof(int16_t));
	result.append(typeName.c_str(), nameLen);
	bool success = true;
	//设置 protobufData
	if(flag_.test(PROTO_FORMAT_INDXE))
	{
		protobuf::util::JsonOptions options;
		//options.add_whitespace = true;	
		if(flag_.test(PROTO_ZIP_INDXE))
		{
			std::string json = "";
			success = MessageToJsonString(message, &json, options).ok();
			uint64_t ziplen = compressBound(json.size());			
			std::shared_ptr<Bytef> zip(new Bytef[ziplen + 1]);
			int err = compress(zip.get(),&ziplen,(const Bytef*)json.c_str(),json.size());
			if(err != Z_OK)
			{
				success = false;
			}
			else
			{
				result.append((const char *)zip.get(),ziplen);
			}
		}
		else
		{
			success = MessageToJsonString(message, &result, options).ok();
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
	int32_t checkSum = checksum_f(1,
       reinterpret_cast<const Bytef*>(result.c_str() + HEAD_LEN), 
	   result.size() - HEAD_LEN);
	checkSum = ::htonl(checkSum);
	result.append(reinterpret_cast<char*>(&checkSum), sizeof(int32_t));
		
	//网络序数据包长度
	int32_t len = ::htonl(result.size() - HEAD_LEN);
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

void addPerson(tutorial::AddressBook &address_book)
{
	tutorial::Person* person = address_book.add_people();
	assert(person);	
	person->set_id(100);
	*person->mutable_name() = "huang";
	person->set_email("888888@qq.com");
	//
	tutorial::Person::PhoneNumber* phone_number = person->add_phones();
    phone_number->set_number("18912345678");
	phone_number->set_type(tutorial::Person::MOBILE);

	phone_number = person->add_phones();
    phone_number->set_number("075512345678");
	phone_number->set_type(tutorial::Person::WORK);	
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
	packet.set_proto_checksum_algorithm(true);
	packet.set_proto_format(true);
	packet.set_proto_zip(true);
	std::string buf = packet.encode(address_book);

	buf.append("dafdfdsa");
	tutorial::AddressBook *book = dynamic_cast<tutorial::AddressBook*>(packet.decode(buf));  
	for(int i = 0;i < book->people_size();++i)
	{
		const tutorial::Person &person = book->people(i);
		std::cout<<"id:"<<person.id()<<endl;
		std::cout<<"e-mail:"<<person.email()<<endl;
		for(int k = 0;k< person.phones_size();++k)
		{
			const tutorial::Person_PhoneNumber &number = person.phones(k);
			std::cout<<"number:"<<number.number()<<endl;
			std::cout<<"number type:"<<number.type()<<endl;
		}
	}
	

	// Optional:  Delete all global objects allocated by libprotobuf.
  	google::protobuf::ShutdownProtobufLibrary();

	return 0;
}
