package protobufpacket;

import java.io.IOException;
import java.nio.ByteBuffer;

import com.google.protobuf.*;
import com.google.gson.JsonIOException;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.google.gson.JsonSyntaxException;

public class JsonPacket {
	public String encode(Message message) throws IOException {
		ProtobufPacket packet = new ProtobufPacket();
		packet.set_proto_format(true);
		packet.encode(message);

		StringBuilder json = new StringBuilder();
		json.append("{");
		json.append(LENGTH + ":").append(packet.get_packet_length()).append(",");
		json.append(FLAG + ":").append(packet.get_packet_flag()).append(",");
		json.append(NAMELEN + ":").append(packet.get_packet_typeName_length()).append(",");
		json.append(TYPENAME).append(":\"").append(packet.get_packet_typeName()).append("\",");
		json.append(PB_DATA + ":").append(packet.get_json_message()).append(",");
		json.append(CHECKSUM + ":").append(packet.get_check_sum());
		json.append("}");

		return json.substring(0);
	}

	public byte[] decode(String jsondata) throws IOException {
		byte[] protobuf = null;
		JsonParser parse =new JsonParser();		
		 try {
	            JsonObject json=(JsonObject) parse.parse(jsondata);
	            int length = json.get(LENGTH).getAsInt();
	            
	            int start = jsondata.indexOf(PB_DATA);
	            int end = jsondata.lastIndexOf(CHECKSUM);
	            String pb_data = jsondata.substring(start + PB_DATA.length() + 1, end - 1);	            

	    		ByteBuffer databuf = ByteBuffer.allocate(length + Integer.SIZE / 8);
	    		// 使用网络字节序
	    		databuf.order(java.nio.ByteOrder.BIG_ENDIAN);
	    		databuf.putInt(length);
	    		databuf.putShort(json.get(FLAG).getAsShort());
	    		databuf.putShort(json.get(NAMELEN).getAsShort());
	    		databuf.put(json.get(TYPENAME).getAsString().getBytes());
	    		databuf.put((byte) '\0');
	    		databuf.put(pb_data.getBytes());
	    		databuf.putInt(json.get(CHECKSUM).getAsInt());
	            
	            ProtobufPacket packet = new ProtobufPacket();	            
	            protobuf = packet.decode(databuf.array());	            
	        } catch (JsonIOException e) {
	            e.printStackTrace();
	        } catch (JsonSyntaxException e) {
	            e.printStackTrace();
	        }
		return protobuf;
	}

	private String LENGTH = "Length";
	private String FLAG = "Flag";
	private String NAMELEN = "NameLen";
	private String TYPENAME = "TypeName";
	private String PB_DATA = "PB_Data";
	private String CHECKSUM = "CheckSum";
}

