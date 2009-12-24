#!/usr/bin/perl
use Test::More;
use JavaScript::V8;
my $context = JavaScript::V8::Context->new();
ok $context,"creating new JavaScript::V8::Context";
$context->eval("1");
done_testing;
