
require('./addressbook_pb');
require('./encoding-indexes');
var encoding = require('./encoding');

/** @param {!proto.protocol.AddressBook} book_ */
print_AddressBook = function(book_) {
	for(var i = 0; i < book_.getPeopleList().length;++i)
	{
		console.log("\n");
		person = book_.getPeopleList()[i];
		for(var k = 0; k < person.getPhonesList().length;++k)
		{
			console.log("photo type:",person.getPhonesList()[k].getType());
			console.log("photo number:",person.getPhonesList()[k].getNumber());
		}
		console.log("person name:",person.getName());
		console.log("person id:",person.getId());
		console.log("person name:",person.getEmail());	
	}
}

/**
 * Convert an Uint8Array into a string.
 *
 * @returns {String}
 */
function Decodeuint8arr(uint8array){
    var d = new encoding.TextDecoder("utf-8");
    return d.decode(uint8array);
}

/**
 * Convert a string into a Uint8Array.
 *
 * @returns {Uint8Array}
 */
function Encodeuint8arr(myString){
    var e = new encoding.TextEncoder("utf-8");
    return e.encode(myString);
}

var person_1 = new proto.protocol.Person();
person_1.setName("huang马克黄法电风扇^&043240%^89420");
person_1.setId(100);
person_1.setEmail("888888@qq.com");

var book = new proto.protocol.AddressBook();

//加入第一个人
person_2 = book.addPeople();
person_2.setName("huang马克黄法电风扇^&043240%^89420");
person_2.setId(100);
person_2.setEmail("888888@qq.com");

//比较序列化结果
if(person_1.serializeBinary().length == person_2.serializeBinary().length)
{
	console.log("serializeBinary equal");	
	for(var i = 0;i < person_1.serializeBinary().length;i++)
	{
		if(person_1.serializeBinary()[i] != person_2.serializeBinary()[i])
		{
			console.log("not equal");			
		}
	}	
}

//增加其他信息
var photo = person_1.addPhones();
photo.setType(proto.protocol.Person.PhoneType.MOBILE);
photo.setNumber("18912345678");
photo = person_1.addPhones();
photo.setType(proto.protocol.Person.PhoneType.WORK);
photo.setNumber("075512345678");

//增加第二个人
book.addPeople(person_1);

photo = person_2.addPhones();
photo.setType(proto.protocol.Person.PhoneType.MOBILE);
photo.setNumber("18912345678");
photo = person_2.addPhones();
photo.setType(proto.protocol.Person.PhoneType.WORK);
photo.setNumber("075512345678");

//var json = JSON.stringify(message);
//console.log(json);

//序列化
var bytes = book.serializeBinary();
console.log("serializeBinary:",bytes.length);

//var ss = book.serializeString_();
//goog.json.Serializer.prototype.serializeString_(book,ss);

//反序列化
var book_ = new proto.protocol.AddressBook.deserializeBinary(bytes);
print_AddressBook(book_)
console.log('print_AddressBook has finished\n');
//console.log(Decodeuint8arr(bytes));
console.log(bytes.toString());

//输出为 二进制文件,为其他语言解析
var fs = require('fs');
fs.writeFile('./js_out',Decodeuint8arr(bytes),function(err)
{
	if(err) throw err;
	console.log('writeFile js_out has finished\n');
});

//解析 C++ 输出的protobuf数据序列化的二进制文件数据
fs.readFile("./cpp_out",function(err,bytes){
	if(err) throw err;
	if(bytes.length > 0)
	{
		//反序列化
		console.log("aa serializeBinary:",bytes.length);
		arr = Encodeuint8arr(bytes);
		book_ = new proto.protocol.AddressBook.deserializeBinary(arr);
		print_AddressBook(book_);
	}
});

