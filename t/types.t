#!/usr/bin/perl

use Test::More;
use JavaScript::V8;

use utf8;
use strict;
use warnings;

my $context = JavaScript::V8::Context->new;

is $context->eval('(function(v) { return v })')->(1000000000000), 1000000000000, 'big numbers are ok';
is $context->eval('(function(v) { return v })')->(2.34234), 2.34234, 'real numbers are ok';

done_testing;
