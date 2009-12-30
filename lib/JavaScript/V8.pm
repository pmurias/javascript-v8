package JavaScript::V8;
use v5.10;
use strict;
use warnings;

our $VERSION = '0.02';

require XSLoader;
XSLoader::load('JavaScript::V8', $VERSION);

1;
__END__
=encoding utf8

=head1 NAME

JavaScript::V8 - Perl interface to the V8

=head1 SYNOPSIS

  use JavaScript::V8;

  my $context = JavaScript::V8::Context->new();

  $context->bind_function(write => sub { print @_; });

  $context->eval(q/
    for (i = 99; i > 0; i--) {
        write(i + " bottle(s) of beer on the wall, " + i + " bottle(s) of beer\n");
        write("Take 1 down, pass it around, ");
        if (i > 1) {
            write((i - 1) + " bottle(s) of beer on the wall.");
        }
        else {
            write("No more bottles of beer on the wall!");
        }
    }
  /);

=head1 INSTALLING V8

See http://code.google.com/p/v8/issues/detail?id=413 if you are having trouble on gcc 4.4.1

    svn checkout http://v8.googlecode.com/svn/trunk/ v8
    cd v8
    scons 
    sudo mv include/v8.h /usr/local/include/
    sudo mv libv8.a /usr/local/lib/

=head1 REPOSITORY

The source code lives at http://github.com/pmurias/javascript-v8.

=head1 AUTHORS

  Pawel Murias <pawelmurias at gmail dot com>

=head1 COPYRIGHT AND LICENSE

  Copyright (c) 2009 Paweł Murias

This library is free software; you can redistribute it and/or modify
it under the same terms as Perl itself

=head1 ACKNOWLEDGMENTS

=over

=item Claes Jakobsson <claesjac at cpan dot org>
I stole and adapted pieces of docs and API design from JavaScript.pm

=item Brian Hammond <brain @ fictorial dot com>
For salvaging the code of V8.pm from a message board (which i took some code and the idea from)

=item The hacker who wrote V8.pm and posted it on the message board (http://d.hatena.ne.jp/dayflower/20080905/1220592409)

=item All the fine people at #perl@freenode.org for helping me write this module

=back
=cut

