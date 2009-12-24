#!/usr/bin/perl
use Test::More;
use JavaScript::V8;
use utf8;
use strict;
use warnings;

my $context = JavaScript::V8::Context->new();
ok $context,"creating new JavaScript::V8::Context";
my $val = $context->eval("777");
is $val,777,"integers";

is($context->eval("'Μπορώ να φάω σπασμένα γυαλιά χωρίς να πάθω τίποτα'"), "Μπορώ να φάω σπασμένα γυαλιά χωρίς να πάθω τίποτα", 'unicode strings');

is($context->eval("1.34"), 1.34, 'numbers');
ok(!defined $context->eval('undefined'), 'undefined');
ok(!defined $context->eval('null'), 'null');
my $called_foo = 0;
sub foo {
   $called_foo = 1; 
   'arg';
}
sub bar {
    is $_[0],'arg','passing arguments and return values';
}
$context->register_method_by_name('foo');
$context->register_method_by_name('bar');
$context->eval("foo()");
ok($called_foo);
$context->eval("bar(foo())");

done_testing;
