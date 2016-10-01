# epl v0.15
**在Emacs多态模块中使用Perl解释器**

- 在elisp中调用perl函数
- 在perl中调用elisp函数

![testing](img/epl_test.jpg)
## 安装
需要Emacs25以上版本,并且需要选择--with-modules选项
Windows上也要自己编译, 因为目前(09/29/2016)官方预编译的版本好像还是没有启用这个选项的.

### Linux/Unix 上需要对emacs源码稍作修改, windows上无须修改
emacs-src/src/dynlib.c 在大约276行 (dynlib_open函数中):
```
 return dlopen (path, RTLD_LAZY);
 改成
 return dlopen (path, RTLD_LAZY|RTLD_GLOBAL);
```
否则就不能加载带有XS的perl模块.

### 编译epl
直接make吧, Windows上安装strawberryperl然后gmake.

### 加载
将epl目录放在你喜欢的位置, 然后
```
(add-to-list 'load-path "path/to/epl")
(require 'subr-x);; 需要这个
(require 'epl)
```
如果一切顺利, 他会自动去加载epl程序目录下的init.pl,你可以init.pl里面添加你的perl代码,
也可以在emacs里加载其他的perl程序.

### 使用
- (epl-load "perl/code/file") ;;加载一个perl脚本
- (epl-exec "MyMod::myfunc" "args1" ...) ;;执行一个perl函数
- EPL::elisp_exec( "elispFunction" "args1" ...) #在perl中调用elisp函数
- EPL::log(...) #在perl中向emacs的buffer中写入数据

### 类型转换
- Elisp string/integer/float <-> perl scalar
- Elisp vector <-> perl array ref
- Elisp hash-table <-> perl hash ref
- Elisp nil <-> perl undef
- Elisp t -> perl int 1
- 空的perl array ref和hash ref都转换成elisp nil
- Perl的对象也可以返回给elisp,也可以再次传递给perl函数

### 限制
- 只支持elisp的string, integer, float, vector, hash-table, nil和t转换到perl
- perl函数最多只能返回一个值, 但是不能返回多个值(数组).

### 问题
- 在linux字符界面的情况下,耗时长的perl操作可能会被终止, 比如sleep(99),再移动光标sleep就会结束.
要是想使用DBI等模块就要慎重了, 没试过linux上的GUI会不会这样, elisp有什么函数可以然程序一直阻塞运行么?
- Windows上perl可能遇到@INC为空的情况,但是有的情况又不会, 在init.pl理用BEGIN{}添加@INC就可以解决了.

### 警告
这个程序还仅在测试阶段, 可能会导致emacs崩溃并损失buffer中未保存的内容!

### TODO
也许需要epl-destroy epl-reload-perl这样的功能来重新创建perl解释器让世界清静.

### 参考
- emacs-mruby-test (https://github.com/syohex/emacs-mruby-test).
- weechat (https://weechat.org/) 很好的IRC客户端.

