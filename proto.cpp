
#include "proto.h"
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/descriptor_database.h>
#include <google/protobuf/dynamic_message.h>

#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/type_resolver.h>
#include <google/protobuf/util/type_resolver_util.h>

#include <zlib.h>  // adler32,crc32
#include <sstream>
#include <arpa/inet.h>  // htonl, ntohl
#include <stdint.h>

using namespace std::placeholders;
using namespace google;
using namespace std;

const std::string CProtobufPacket::encode(const Message& message)
{
	std::string result = "";
	result.resize(HEAD_LEN);
	
	//设置 flag	
	int16_t be16 = htons(flag_.to_ulong());
	result.append(reinterpret_cast<char*>(&be16),sizeof(int16_t));

	//设置 nameLen和typeName(\0结尾)
	typeName_ = message.GetTypeName();	
	typeName_length_ = static_cast<int16_t>(typeName_.size() + 1);
	be16 = htons(typeName_length_);
	result.append(reinterpret_cast<char*>(&be16),sizeof(int16_t));
	result.append(typeName_.c_str(), typeName_length_);
	bool success = true;
	//设置 protobufData
	if(flag_.test(PROTO_FORMAT_INDXE))
	{
		protobuf::util::JsonOptions options;
		//options.add_whitespace = true;		
		success = MessageToJsonString(message, &json_message_, options).ok();		
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
				//增加压缩率标志,便于解压分配内存
				uint32_t ratio = json_message_.size()/ziplen;
				ratio = htonl(ratio);
				result.append((char*)(&ratio),sizeof(uint32_t));
				result.append((char*)zip.get(),ziplen);
			}			
		}
		else
		{
			result.append(json_message_);
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

	std::function<size_t (size_t,const Bytef*,size_t)> 
	checksum_f = std::bind(::adler32,_1,_2,_3);	
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

protobuf::Message* CProtobufPacket::decode(const std::string& buf)
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
  std::function<size_t (size_t,const Bytef*,size_t)> 
	checksum_f = std::bind(::adler32,_1,_2,_3);	
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
  protobuf::Message* message = createMessage(typeName);
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

inline Message* CProtobufPacket::createMessage(const std::string& type_name)
{
  google::protobuf::Message* message = nullptr;
  const protobuf::Descriptor* descriptor =
    protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(type_name);
  if (descriptor)
  {
    const protobuf::Message* prototype =
      protobuf::MessageFactory::generated_factory()->GetPrototype(descriptor);
    if (prototype)
    {
      message = prototype->New();
    }
  }
  return message;
}

inline bool CProtobufPacket::unzip(std::string &json)
{
	//解析出压缩率
	uint64_t unziplen = asInt32(json.c_str());	
	unziplen = (unziplen + 1) * json.size() + 1;	
	shared_ptr<Bytef> unzip(new Bytef[unziplen]);
	int err = uncompress(unzip.get(),&unziplen,
	(const Bytef *)json.c_str() + sizeof(uint32_t),
	json.size() -  sizeof(uint32_t));
	if(err == Z_OK)
	{
		json.clear();
		json.resize(unziplen);
		unzip.get()[unziplen] = '\0';
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

void CProtobufPacket::set_proto_checksum_algorithm(bool adler32)
{
	flag_[CHECKSUM_ALGORITHM_INDXE] = adler32 ? 0 : 1;
}

bool CProtobufPacket::get_proto_checksum_algorithm()
{
	return flag_.test(CHECKSUM_ALGORITHM_INDXE);
}

void CProtobufPacket::set_proto_format(bool json)
{
	flag_[PROTO_FORMAT_INDXE] = json ? 1 : 0;
}

bool CProtobufPacket::get_proto_format()
{
	return flag_.test(PROTO_FORMAT_INDXE);
}

void CProtobufPacket::set_proto_zip(bool zip)
{
	flag_[PROTO_ZIP_INDXE] = zip ? 1 : 0;
}

bool CProtobufPacket::get_proto_zip()
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

const std::string CJsonPacket::encode(const Message& message)
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

protobuf::Message* CJsonPacket::decode(const std::string& json)
{
	size_t start = 0;
	int32_t be32 = 0,namelen = 0;
	be32 = std::stoi(get_value(json,LENGTH,start),nullptr,0);
	std::string buff = "";
	buff.resize(be32 + sizeof(int32_t));
	buff.clear();
	be32 = htonl(be32);
	buff.append(reinterpret_cast<char*>(&be32),sizeof(int32_t));

	be32 = htons(std::stoi(get_value(json,FLAG,start),nullptr,0));
	buff.append(reinterpret_cast<char*>(&be32),sizeof(int16_t));

	namelen = std::stoi(get_value(json,NAMELEN,start),nullptr,0);
	be32 = htons(namelen);
	buff.append(reinterpret_cast<char*>(&be32),sizeof(int16_t));
	
	const std::string &s = get_value(json,TYPENAME,start);
	assert(s.size() > 2);	
	buff.append(s.substr(1,s.size() - 2).c_str(), namelen);

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
		const std::string &s = json.substr(start,end - start);		
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

