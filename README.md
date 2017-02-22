# protobufpacket
protobuf c++ 2011 packet

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
