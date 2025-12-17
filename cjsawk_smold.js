Uint8Array = Array;

read_=read;

read=function(x,y){
  if(arguments.length>1){
    if(y==="binary"){
      var b=read_(x);
      b=b.split("");
      for(var i = 0; i< b.length;i++) {
        b[i] = b[i].charCodeAt(0);
      }
      return b;
    }
  }
  return read_(x);
};

function gen_out2(){
  if(out_file[out_file.length-1]=== mkc("\n")){
   out_file.pop();
  }
  for(var i = 0; i < out_file.length;i++) {
    out_file[i] = String.fromCharCode(out_file[i]);
  }
  return out_file.join("");
}

load_ = load;

dlsym_ptr = get_dlsym();

function dlsym(handle, name) {
  return ffi_call(dlsym_ptr, handle, name);
}

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

(function() {
  var heap_ = libc.calloc(16*1024*1024, 1);

  function wi8(o,v) {
    poke8(o+heap_, v);
  }

  function ri8(o) {
    return peek8(o+heap_, o);
  }

  function wi32(o,v) {
    poke32(o+heap_, v);
  }

  function ri32(o) {
    return peek32(o+heap_, o);
  }

  wi8_ = wi8;
  ri8_ = ri8;
  wi32_ = wi32;
  ri32_ = ri32;
})();

load = function(name) {
//  print("load: " + name);
  load_(name);
  if(name === "cjsawk.js") {
    wi8 = wi8_;
    ri8 = ri8_;
    wi32 = wi32_;
    ri32 = ri32_;
//    gen_out = function(){return "";};
  }
  return;
}

function write_file(oname, data) {
  var t = libc.calloc(data.length, 1);
  if(oname === undefined) {
    throw "oname is undefined";
  }
  for(var i = 0; i <data.length; i++) {
    poke8(t+i, data[i]);
  }
  var f = libc.fopen(oname, "wb");
  libc.fwrite(t, 1, data.length, f);
  libc.fclose(f);
}

/* FIXME this try catch is a bodge and will swallow errors */
try {
  fname = arguments[0];
  load("cjsawk_test.js");
} catch(e) {
  print(gen_out2());
}
