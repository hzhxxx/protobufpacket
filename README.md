# protobufpacket
protobuf c++ 2011 packet

鉴于通过 protobuf 序列化的数据没有数据包头，不便于通过网络发送并识
别，故制定本规则。

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
装载  protobuf 数据（类)通过序列化后的的实例。

5. 支持 http(https)+json 格式,包头采用http和https协议封装,支持Json 打包和解包,类似如下格式
{"Length":311,"Flag":2,"NameLen":21,"TypeName":"tutorial.AddressBook","PB_Data":{Json数据},"CheckSum":-2121440958}

6. 如果是 java 版本,需要获取到 descriptor_set_out 文件,用于动态构造 Message
对象,以官方例子 addressbook.proto 文件为例,生成方法如下:
protoc --descriptor_set_out=Protobuf.desc --java_out=./ addressbook.proto
修改类 ProtobufPacket 的私有成员 
private String DescFileName_ = "D:\\study\\language\\java\\protobufpacket\\src\\tutorial\\Addressbook.desc";
为自有项目中 descriptor_set_out 文件的名称即可,默认位于./src/protocol/Protobuf.desc,
可关注 .proto内的这几项
package protocol;
option java_package = "protocol";
option java_outer_classname = "Protobuf";

7. 鉴于 google protobuf 目前只有部分语言(C++,java/python等)实现了 Json 和二进制对象的互转,
对于非Json ,可以直接通过 HTTP 二进制传输,通过 Content-Type 标识,
application/json    JSON数据格式
application/x-protobuf    protobuf格式数据 