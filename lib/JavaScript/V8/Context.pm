package JavaScript::V8::Context;

sub new {
    my($class, %args) = @_;

    my $time_limit = delete $args{time_limit} || 0;
    $class->_new($time_limit);
}

sub bind_function {
    my $class = shift;
    $class->bind(@_);
}

1;

=head1 NAME

JavaScript::V8::Context - An object in which we can execute JavaScript

=head1 SYNOPSIS

  use JavaScript::V8;

  # Create a runtime and a context
  my $context = JavaScript::V8::Context->new();

  # Add a function which we can call from JavaScript
  $context->bind_function(print => sub { print @_ });

  my $result = $context->eval($source);

=head1 INTERFACE

=over

=item new ( [time_limit => seconds] )

Create a new JavaScript::V8::Context object. The optional C<time_limit>
parameter will force an exception after the script has run for a number of
seconds; this limit will be enforced even if V8 calls back to Perl or blocks on
IO.

=item bind ( name => $scalar )

Converts the given scalar value (array ref, code ref, or hash ref) to a v8
value and binds it in this context.

=item bind_function ( $name => $subroutine_ref )

DEPRECATED. This is just an alias for bind.

=item eval ( $source )

Evaluates the JavaScript code given in I<$source> and
returns the result from the last statement.

If there is a compilation error (such as a syntax error) or an uncaught exception
is thrown in JavaScript, this method returns undef and $@ is set.

=back

=cut
