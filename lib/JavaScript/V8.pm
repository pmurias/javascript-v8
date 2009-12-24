package JavaScript::V8;
use 5.006001;
use strict;
use warnings;

our $VERSION = '0.01';

require XSLoader;
XSLoader::load('JavaScript::V8', $VERSION);

1;
