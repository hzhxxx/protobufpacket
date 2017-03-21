
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
*/

package protobufpacket;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.BitSet;
import java.util.HashMap;
import java.util.Map;
import java.util.zip.*;

import com.google.protobuf.DescriptorProtos.FileDescriptorProto;
import com.google.protobuf.DescriptorProtos.FileDescriptorSet;
import com.google.protobuf.Descriptors.Descriptor;
import com.google.protobuf.Descriptors.FileDescriptor;
import com.google.protobuf.DynamicMessage;
import com.google.protobuf.DynamicMessage.Builder;
import com.google.protobuf.Message;
import com.google.protobuf.util.*;
import java.util.concurrent.atomic.AtomicInteger;

public final class ProtobufPacket {
	public byte[] encode(Message message) throws IOException {
		// 设置 nameLen和typeName(\0结尾)
		typeName_ = message.getClass().getName();
		int posend = typeName_.lastIndexOf('$');
		int posstart = typeName_.lastIndexOf(".");
		if (posend != -1 && posstart != -1) {
			String fileClassName = typeName_.substring(posstart, posend + 1);
			typeName_ = typeName_.replace(fileClassName, ".");
		}
		typeName_length_ = (short) (typeName_.length() + 1);

		// 设置 protobufData
		byte[] ProtobufData = null;
		if (flag_.get(PROTO_FORMAT_INDXE)) {
			json_message_ = JsonFormat.printer().omittingInsignificantWhitespace().print(message);
			if (flag_.get(PROTO_ZIP_INDXE)) {
				ByteArrayOutputStream bos = new ByteArrayOutputStream();
				ZipOutputStream zip = null;
				zip = new ZipOutputStream(bos);
				ZipEntry entry = new ZipEntry("zip");
				entry.setSize(json_message_.getBytes().length);
				zip.putNextEntry(entry);
				zip.write(json_message_.getBytes());
				zip.closeEntry();
				zip.close();
				ProtobufData = bos.toByteArray();
				bos.close();
			} else {
				ProtobufData = json_message_.getBytes();
			}
		} else {
			ProtobufData = message.toByteArray();
		}

		if (ProtobufData.length <= 0) {
			return null;
		}

		// 构造校验数据
		packet_length_ = FLAG_LEN + NAME_LEN + typeName_length_ + ProtobufData.length;
		ByteBuffer checkbuf = ByteBuffer.allocate(packet_length_);
		// 使用网络字节序
		checkbuf.order(java.nio.ByteOrder.BIG_ENDIAN);
		// 获取 Flag_
		checkbuf.putShort(this.get_packet_flag());
		checkbuf.putShort(typeName_length_);
		checkbuf.put(typeName_.getBytes());
		checkbuf.put((byte) '\0');
		checkbuf.put(ProtobufData);
		if (flag_.get(CHECKSUM_ALGORITHM_INDXE)) {
			CRC32 crc32 = new CRC32();
			crc32.update(checkbuf.array());
			check_sum_ = (int) crc32.getValue();
		} else {		
			Adler32 adler32 = new Adler32();
			adler32.update(checkbuf.array());
			check_sum_ = (int) adler32.getValue();
		}

		// 构造结果
		packet_length_ = HEAD_LEN + FLAG_LEN + NAME_LEN + typeName_length_ + ProtobufData.length + CHECKSUM_LEN;
		ByteBuffer result = ByteBuffer.allocate(packet_length_);
		packet_length_ = result.array().length - HEAD_LEN;
		checkbuf.order(java.nio.ByteOrder.BIG_ENDIAN);
		result.putInt(packet_length_);
		result.put(checkbuf.array());
		result.putInt(check_sum_);
		return result.array();
	}

	public byte[] decode(byte[] buf) throws IOException {
		ByteBuffer result = ByteBuffer.allocate(buf.length);
		result.put(buf);
		int len = result.getInt(0);
		if (buf.length < MIN_LENGTH || len >= buf.length) {
			return null;
		}

		check_sum_ = result.getInt(len);
		short flag = result.getShort(HEAD_LEN);
		for (int i = 0; i < flag_.size(); ++i) {
			flag_.set(i, (flag >> (i) & 1) == 1 ? true:false);
		}
		int compute_checkSum = 0;
		packet_length_ = len - CHECKSUM_LEN;
		byte[] checkbuf = new byte[packet_length_];
		result.position(HEAD_LEN);
		result.get(checkbuf, 0, packet_length_);
		if (flag_.get(CHECKSUM_ALGORITHM_INDXE)) {
			CRC32 crc32 = new CRC32();
			crc32.update(checkbuf);
			compute_checkSum = (int) crc32.getValue();
		} else {
			Adler32 adler32 = new Adler32();
			adler32.update(checkbuf);
			compute_checkSum = (int) adler32.getValue();
		}

		if (check_sum_ != compute_checkSum) {
			return null;
		}

		short nameLen = result.getShort(HEAD_LEN + FLAG_LEN);
		if (nameLen < 2 || nameLen > len - 2 * HEAD_LEN) {
			return null;
		}
		
		byte[] Name = new byte[nameLen - 1];
		result.position(HEAD_LEN + FLAG_LEN + NAME_LEN);
		result.get(Name, 0, nameLen - 1);
		typeName_ = new String(Name);		
		int dataLen = len - FLAG_LEN - NAME_LEN - CHECKSUM_LEN - nameLen;
		byte[] data = new byte[dataLen];
		result.position(HEAD_LEN + FLAG_LEN + NAME_LEN + nameLen);
		result.get(data, 0, dataLen);
		boolean success = true;
		if (flag_.get(PROTO_FORMAT_INDXE)) {			
			String json = new String(data);
			if (flag_.get(PROTO_ZIP_INDXE)) {
				json = unzip(data);
			}
			Message.Builder builder = createMessage(typeName_);
			JsonFormat.parser().merge(json, builder);
			data = builder.build().toByteArray();			
		} 
		packet_length_ = len;
		return success ? data : null;
	}

	public final void set_proto_checksum_algorithm(boolean adler32) {
		flag_.set(CHECKSUM_ALGORITHM_INDXE, adler32);
	}

	public final void set_proto_format(boolean json) {
		flag_.set(PROTO_FORMAT_INDXE, json);
	}

	public final void set_proto_zip(boolean zip) {
		flag_.set(PROTO_ZIP_INDXE, zip);
	}

	public final boolean get_proto_checksum_algorithm() {
		return flag_.get(CHECKSUM_ALGORITHM_INDXE);
	}

	public final boolean get_proto_format() {
		return flag_.get(PROTO_FORMAT_INDXE);
	}

	public final boolean get_proto_zip() {
		return flag_.get(PROTO_ZIP_INDXE);
	}

	public final int get_packet_length() {
		return packet_length_;
	}

	public final short get_packet_flag() {
		byte[] bytes = flag_.toByteArray();
		short length = 0;
		if (bytes.length > 0) {
			length |= bytes[0] & 0xFF;
		}
		if (bytes.length > 1) {
			length |= (bytes[1] & 0xFF) << 8;
		}

		return length;
	}

	public final String get_packet_typeName() {
		return typeName_;
	}

	public final short get_packet_typeName_length() {
		return typeName_length_;
	}

	public final String get_json_message() {
		return json_message_;
	}

	public final int get_check_sum() {
		return check_sum_;
	}

	private int HEAD_LEN = Integer.SIZE / 8;
	private short FLAG_LEN = Short.SIZE / 8;
	private short NAME_LEN = Short.SIZE / 8;
	private short CHECKSUM_LEN = Integer.SIZE / 8;
	private short CHECKSUM_ALGORITHM_INDXE = 0;
	private short PROTO_FORMAT_INDXE = 1;
	private short PROTO_ZIP_INDXE = 2;
	private int MIN_LENGTH = HEAD_LEN + FLAG_LEN + NAME_LEN + CHECKSUM_LEN;
	private int packet_length_ = 0;
	private short typeName_length_ = 0;
	private int check_sum_ = 0;
	private String typeName_ = "";
	private String json_message_ = "";
	private BitSet flag_ = new BitSet(16);
	private static AtomicInteger descriptors_Lock = new AtomicInteger(0);
	private static Map<String, Descriptor> descriptors_ = new HashMap<String, Descriptor>();
	private String DescFileName_ = "./src/protocol/Protobuf.desc";

	private Message.Builder createMessage(String type_name) {
		BuildDescriptor();
		Descriptor desc = descriptors_.get(type_name);
		Builder builder = DynamicMessage.newBuilder(desc);	
		return builder;
	}

	private void BuildDescriptor() {
		// 确保整个应用只初始化一次
		if (descriptors_.size() > 0 || 1 < descriptors_Lock.incrementAndGet()) {
			return;
		}

		try {
			try {				
				FileInputStream fs = new FileInputStream(DescFileName_);
				FileDescriptorSet descriptorSet = FileDescriptorSet.parseFrom(fs);
				for (FileDescriptorProto descproto : descriptorSet.getFileList()) {
					FileDescriptor fd = FileDescriptor.buildFrom(descproto, new FileDescriptor[] {});
					for (Descriptor descriptor : fd.getMessageTypes()) {
							System.out.println(descriptor.getFullName());
							descriptors_.put(descriptor.getFullName(), descriptor);
					}
				}
			} catch (IOException e) {
				System.out.println(e.toString());
			}
		} catch (Exception e) {
			System.out.println(e.toString());
		}
	}

	private String unzip(byte[] json) {
		ByteArrayOutputStream out = new ByteArrayOutputStream();
		ByteArrayInputStream in = new ByteArrayInputStream(json);
		ZipInputStream zin = new ZipInputStream(in);
		try {
			while (zin.getNextEntry() != null) {
				byte[] buffer = new byte[1024];
				int offset = -1;
				while ((offset = zin.read(buffer)) != -1) {
					out.write(buffer, 0, offset);
				}
			}
		} catch (IOException e) {
			//
		} finally {
			try {
				if (zin != null) {
					zin.close();
				}
				if (in != null) {
					in.close();
				}
				if (out != null) {
					out.close();
				}
			} catch (IOException e) {
				e.printStackTrace();
			}
		}
		return out.toString();
	}
}

