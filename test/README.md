# Test contract

0.1.0 has one package regression test. It creates the extension, checks its
version and comment, and proves that it owns no API objects. It then drops the
extension and checks that PostgreSQL removed it.

This is the complete test boundary for 0.1.0. The extension has no behavior
API in this version.
