function to_hex(x) {
  return "0x"+((x>>>0).toString(16));
}

dlsym_ptr = get_dlsym();

function dlsym(handle, name) {
  return ffi_call(dlsym_ptr, handle, name);
}

print("dlsym_ptr: "+to_hex(dlsym_ptr));

puts = (function() {
  var puts_ptr = dlsym(0, "puts");
  return function(str) {
    return ffi_call(puts_ptr, str);
  };
})();

puts("Hello world via ffi");

libc = {};

libc.calloc = (function() {
  var calloc_ptr = dlsym(0, "calloc");
  return function(nmemb, size) {
    return ffi_call(calloc_ptr, nmemb, size);
  };
})();

libc.fopen = (function() {
  var fopen_ptr = dlsym(0, "fopen");
  return function(path, mode) {
    return ffi_call(fopen_ptr, path, mode);
  };
})();

libc.fwrite = (function() {
  var fwrite_ptr = dlsym(0, "fwrite");
  return function(ptr, size, nmemb, stream) {
    return ffi_call(fwrite_ptr, ptr, size, nmemb, stream);
  };
})();

libc.fclose = (function() {
  var fclose_ptr = dlsym(0, "fclose");
  return function(stream) {
    return ffi_call(fclose_ptr, stream);
  };
})();

m = libc.calloc(1024, 1);

poke8(m, 65);
poke8(m+1, 0);

puts(m);
