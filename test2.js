function to_hex(x) {
  return "0x"+((x>>>0).toString(16));
}

print("dlopen: "+to_hex(get_dlopen()));
