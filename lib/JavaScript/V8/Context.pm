=head1 NAME

JavaScript::V8::Context - An object in which we can execute JavaScript

=head1 SYNOPSIS

  use JavaScript::V8;

  # Create a runtime and a context
  my $context = JavaScript::V8::Context->new();

  # Add a function which we can call from JavaScript
  $context->bind_function(print => sub { print @_; });

  my $result = $context->eval($source);

=head1 INTERFACE

=item bind_function ( $name => $subroutine_ref )

Defines a Perl subroutine ($subroutine_ref) as a native function with the given I<$name>.

=item eval ( $source )

Evaluates the JavaScript code given in I<$source> and
returns the result from the last statement.

If there is a compilation error (such as a syntax error) or an uncaught exception
is thrown in JavaScript this method returns undef and $@ is set.
