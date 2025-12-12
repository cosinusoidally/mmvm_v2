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

/* FIXME this try catch is a bodge and will swallow errors */
try {
  load("cjsawk_test.js");
} catch(e) {
  print(gen_out2());
}
