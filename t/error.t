#!/usr/bin/perl
use Test::More;
use JavaScript::V8;
use utf8;
use strict;
use warnings;

my $context = JavaScript::V8::Context->new();
$context->eval('throw "foobarbaz"');
ok $@,'$@ is set';
like $@,qr/foobarbaz/,'message contained in exception stringification';

is $context->eval('1'),1;
is $@,undef,'$@ is not set';


done_testing;

