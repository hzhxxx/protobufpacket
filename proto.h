

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
第3位用来标识是protobufData是否是标准Protobuf对象序列化数据，0:是(默认),1:不是;
第4位用来标识是否支持 PacketLen 长度校验，0:不支持(默认)；1:支持
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

#ifndef PROTO_CPROTOBUF_JSON_Packet_2eproto__INCLUDED
#define PROTO_CPROTOBUF_JSON_Packet_2eproto__INCLUDED

#include <string>
#include <bitset>
#include <algorithm>
namespace google{
	namespace protobuf{
		class Message;		
	}  
}
using google::protobuf::Message;
using namespace std;


//用于支持 TCP 流格式的数据包解析
class CProtobufPacket final
{
public:	
	 const std::string encode(const Message& message);
	 Message* decode(const std::string& buf);	
	 void set_proto_checksum_algorithm(bool adler32);
	 void set_proto_format(bool json);
	 void set_proto_zip(bool zip);
	 bool get_proto_checksum_algorithm();
	 bool get_proto_format();
	 bool get_proto_zip();
	 int32_t get_packet_length();
	 int16_t get_packet_flag();
	 const std::string get_packet_typeName();
	 int16_t get_packet_typeName_length();
	 const std::string get_json_message();
	 int32_t get_check_sum();
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
	Message* createMessage(const std::string& type_name);
	int32_t asInt32(const char* buf);
	int16_t asInt16(const char* buf);
	bool unzip(std::string &json);
};

//用于支持 Json 流格式的数据包解析,比较适用于HTTP(HTTPS)做包头
class CJsonPacket final
{
public:	
	 const std::string encode(const Message& message);	
	 Message* decode(const std::string& json);
private:
	const std::string LENGTH = "\"Length\":";
	const std::string FLAG = "\"Flag\":";
	const std::string NAMELEN = "\"NameLen\":";
	const std::string TYPENAME = "\"TypeName\":";
	const std::string PB_DATA = "\"Data\":";
	const std::string CHECKSUM = "\"Checksum\":";
	 const std::string get_value(
		const std::string &json,
		const std::string& key,size_t &start);
};

#endif

