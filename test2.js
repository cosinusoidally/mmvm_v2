function to_hex(x) {
  return "0x"+((x>>>0).toString(16));
}

dlsym = get_dlsym();

print("dlsym: "+to_hex(dlsym));

puts = ffi_call(dlsym, 0, "puts");

print("puts: "+to_hex(puts));
