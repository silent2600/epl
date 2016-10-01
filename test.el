

(epl-exec "MyTest::test")
(epl-exec "MyTest::noresult")
(epl-exec "MyTest::call_elisp_from_perl_test")

(setq mylist (epl-exec "MyTest::list_test"))
(epl-exec "MyTest::dumpvar" mylist)
(setq myhash (epl-exec "MyTest::hash_test"))
(epl-exec "MyTest::dumpvar" myhash)

(setq myobj (epl-exec "MyTest::new" "MyTest"))
(epl-exec "MyTest::oo_test" myobj)



