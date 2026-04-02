# Note of Virtual Machine

VM 设计：

View 是对于内存资源的引用，不持有所有权，包含类型 .type 和地址 .data

注意数组和指针的语义不一样：数组的.data指向的是缓冲区开始，而指针的.data指向的是一个指针，这个指针指向的才是缓冲区