# 2024-Lab4-ns3-Template

## 目录结构

你可以使用 `git submodule update --init --recursive` 来拉取本仓库的子模块。

本仓库的目录结构如下：

```bash
student@327fb651b54a:~/workspace/code/lab4-starter$ tree -L 3
.
|-- README.md
|-- dumbbell-base.cc
|-- ns-3-tutorial.pdf
`-- test
    |-- README.md
    `-- test-utils
        |-- cwnd-test
        |-- fct-test
        |-- pcap-test
        `-- run_test.sh

5 directories, 5 files
```

其中：
- `dumbbell-base.cc` 为构建哑铃型拓扑的参考代码，你需要基于这个例程进行修改，以完成本 Lab 。
- `ns-3-tutorial.pdf` 是 ns-3.38 的官方教程，供同学们参考。
- `test` 是一个 git submodule，用于存放公开给同学们的测试数据和测试脚本。

`test/test-utils` 目录下存放了用于评测的脚本和部分测试用例，其中：
- `run_test.sh` 是用来运行测试的脚本
- `pcap-test/` 存放了 Exercise 3 的 3 个测试点
- `cwnd-test/` 存放了 Exercise 4 的 5 个测试点
- `fct-test/` 存放了 Exercise 5 的 5 个测试点

## 本地测试

你可以运行 `test/test-utils/run_test.sh <ns3_path> <test_name>` 来进行本地的测试，其中
- `ns3_path` 表示 `ns-3.38` 目录在你的机器上的 **绝对路径** (不是相对路径！！)
- `test_name` 表示你要进行的测试类型，支持的值有 fct、pcap、cwnd、all

举例而言，`test/test-utils/run_test.sh /home/student/workspace/ns-allinone-3.38 cwnd` 可用于单独测试 Exercise 4 (cwnd)，`test/test-utils/run_test.sh /home/student/workspace/ns-allinone-3.38 all` 可用于测试所有需要测试的三个 Exercise (pcap, cwnd, fct) 。

`run_test.sh`必须被放在原位置下运行，不能被移动到其他位置运行。不过你可以在任意工作目录运行它，比如直接用绝对路径运行 `/home/student/workspace/code/lab4-starter/test/run_test.sh /home/student/workspace/ns-allinone-3.38 all` 就是没问题的。

## 如何提交

本 Lab 只需要提交 `dumbbell.cc` 一个文件。你需要直接把它放在本仓库的目录下。最终提交时的目录结构如下：

```bash
student@327fb651b54a:~/workspace/code/lab4-starter$ tree -L 3
.
|-- README.md
|-- dumbbell-base.cc
|-- dumbbell.cc
|-- ns-3-tutorial.pdf
`-- test
    |-- README.md
    `-- test-utils
        |-- cwnd-test
        |-- fct-test
        |-- pcap-test
        `-- run_test.sh

5 directories, 6 files
```

其中只有 `dumbbell.cc` 是你需要新提交的，其他的文件都是本来就有的。我们之后会根据你的 `dumbbell.cc` 进行评测，所以请务必把你提交的作业文件命名为 `dumbbell.cc`！（不然评测脚本会找不到你的答案）

