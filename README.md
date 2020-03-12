# Cache 替换策略实验

## 编程环境

* 操作系统: wsl ubuntu-18.0.4
* 编译环境：gcc version 5.4.0 (GCC); cmake version 3.10.2

## 使用说明

在前人的基础上，修改了 makefile 的默认目标，增加了参考的操作系统和编译环境，并且修改的 Makefile 使之可以正确地执行目标动作。

对项目的结构进行了一下优化，代码都存储在 `src` 目录中，在项目根目录下执行以下指令即可完成编译：

```sh
make build
```

如果已经编译过，且只是对 `src` 中的文件进行了修改，而没有添加或者删除文件，可以使用以下指令进行快速编译：

```sh
make
```

或者使用：

```sh
make update
```

运行时，需要手动输入参数指定 trace 文件的名称，格式如下：

```sh
./bin/CacheSim -i xxx.txt
```
