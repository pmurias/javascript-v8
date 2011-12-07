#!/usr/bin/perl

use Test::More;
use JavaScript::V8;

use utf8;
use strict;
use warnings;

my $context = JavaScript::V8::Context->new();

$context->bind( warn => sub { warn(@_) });

my $c1 = $context->eval(<<'END');
function Counter() {
    this.val = 1;
}

Counter.prototype.inc = function() {
    this.val++;
}

Counter.prototype.get = function() {
    return this.val;
}

Counter.prototype.set = function(newVal) {
    this.val = newVal;
}

Counter.prototype.copyFrom = function(otherCounter) {
    this.set(otherCounter.get());
}

Counter.prototype.__perlPackage = "JS::Counter";

new Counter;
END

isa_ok $c1, 'JS::Counter';
is $c1->get, 1, 'initial value';

$c1->inc;
$c1->inc;

is $c1->get, 3, 'method calls work';

$c1->set('8');

is $c1->get, 8, 'method with an argument';

{
    my $c2 = $context->eval('new Counter');

    $c2->copyFrom($c1);
    $c2->inc;

    is $c2->get, 9, 'converting perl object wrapper back to js';
}

done_testing;
