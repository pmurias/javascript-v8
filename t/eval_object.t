#!/usr/bin/perl
use Test::More tests => 6;
use JavaScript::V8;
use utf8;
use strict;
use warnings;

my $context = JavaScript::V8::Context->new();

{
    my %expected = ( foo => 'bar' );
    is_deeply(\$context->eval('(function(){ return {foo: "bar"}})();'), \%expected);
};

{ 
    my %expected = ( foo => 'bar', boo => 123 );
    is_deeply(\$context->eval('(function(){ return {boo:123,foo: "bar"}})();'), \%expected);
};

{
    my %expected = ( foo => { bar => 'blatt' }, boo => 123 );
    is_deeply(\$context->eval('(function(){ return {foo:{bar:"blatt"},boo:123}})();'), \%expected);
};

{
    my %expected = ( foo => { bar => 'blatt', boo => [123,456] } );
    is_deeply(\$context->eval('(function(){ return {foo:{bar:"blatt",boo:[123,456]}}})();'), \%expected);
};

{
    my %expected = ( foo => [ {} ] );
    is_deeply(\$context->eval('(function(){ return {foo:[{}]}})();'), \%expected);
};

{
    my %expected = ( foo => { bar => { boo => 'far' } } );
    is_deeply(\$context->eval('(function(){ return {foo:{bar:{boo:"far"}}}})();'), \%expected);
};

done_testing;

