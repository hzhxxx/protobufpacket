1.  安装 protobuf-3.2,支持 js 版本,具体见官方网站
https://github.com/google/protobuf/tree/master/js

https://github.com/inexorabletash/text-encoding

2. 使用下列命令构造  addressbook_pb.js
protoc --js_out=import_style=commonjs,binary:. addressbook.proto

注意修改这句
var jspb = require('./google-protobuf');

3. 下列命令运行,查看输出
node hello.js 

4. 数据验证,两种语言输出的二进制数据完全一致
md5sum cpp_out 
0d37e81f1c173db999d1700a2659ffb7  cpp_out
md5sum js_out 
0d37e81f1c173db999d1700a2659ffb7  js_out
