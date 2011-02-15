#!/usr/bin/perl
use Test::More tests => 2;
use JavaScript::V8;
use utf8;
use strict;
use warnings;

my $context = JavaScript::V8::Context->new();
$context->eval('2/;');
ok $@, '$@ is set';
like $@, qr/SyntaxError: Unexpected token/, 'Error returned properly';
