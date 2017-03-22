package protobufpacket;

import java.io.IOException;
import java.nio.ByteBuffer;
import protocol.Protobuf.*;
import protocol.Protobuf.AddressBook;
import protocol.Protobuf.Person.PhoneNumber;

public class Test {
	private static int id = 0;

	static public AddressBook addPerson(AddressBook.Builder address_book) {
		Person.Builder person = Person.newBuilder();
		person.setId(id++);
		person.setName("huang马克黄");
		person.setEmail("888888@qq.com");

		PhoneNumber.Builder phone_number = person.addPhonesBuilder();
		phone_number.setNumber("18912345678");
		phone_number.setType(Person.PhoneType.MOBILE);

		phone_number = person.addPhonesBuilder();
		phone_number.setNumber("075512345678");
		phone_number.setType(Person.PhoneType.WORK);

		address_book.addPeople(person);
		return address_book.build();
	}

	static void print_(AddressBook address_book) {
		for (int i = 0; i < address_book.getPeopleCount(); ++i) {
			Person person = address_book.getPeople(i);
			System.out.println("id:" + person.getId());
			System.out.println("e-mail:" + person.getEmail());
			System.out.println("name:" + person.getName());
			for (int k = 0; k < person.getPhonesCount(); ++k) {
				PhoneNumber number = person.getPhones(k);
				System.out.println("number:" + number.getNumber());
				System.out.println("number type:" + number.getTypeValue());
			}
		}
	}

	public static void main(String[] args) {
		AddressBook.Builder build = AddressBook.newBuilder();
		AddressBook book = addPerson(build);
		for (int i = 0; i < 100; ++i) {
			book = addPerson(build);
		}

		ProtobufPacket packet = new ProtobufPacket();
		packet.set_proto_checksum_algorithm(true);
		packet.set_proto_format(true);
		packet.set_proto_zip(true);

		System.out.println("初始化构造:");
		print_(book);

		byte[] protobuf = null;
		try {
			protobuf = packet.encode(book);
			System.out.println(protobuf.length);
			String TESTBUFF = "附加测试,判定只解析需要的数据";
			ByteBuffer buff = ByteBuffer.allocate(protobuf.length + TESTBUFF.getBytes().length);
			buff.put(protobuf);
			buff.put(TESTBUFF.getBytes());
			protobuf = buff.array();
			protobuf = packet.decode(protobuf);
			book = AddressBook.parseFrom(protobuf);
			System.out.println("序列化后构造:");
			print_(book);
		} catch (IOException e) {
			e.printStackTrace();
		}

		try {
			JsonPacket json = new JsonPacket();
			System.out.println("Json序列化:");
			String jsonprotobuf = json.encode(book);
			System.out.println(jsonprotobuf);
			System.out.println("Json反序列化:");
			protobuf = json.decode(jsonprotobuf);
			book = AddressBook.parseFrom(protobuf);
			print_(book);
		} catch (IOException e) {
			e.printStackTrace();
		}
	}
}