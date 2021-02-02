var ctx = document.querySelector("canvas").getContext("2d"),
    w = ctx.canvas.width,
    h = ctx.canvas.height,
    
    //global array of pixel coordinates for the text
    coords = [];        
    
    filled = [];

//set text color and font
ctx.fillStyle = "rgb(0, 100, 253)";
ctx.font = '88px serif';   

let ctr = 0;

//generate pixel arrays for the text
generate("Hello there. How may I be of assistance", 1000, 1100);  
generate("today?", 2100, 1200);

//console.log(filled);

function generate(txt, x_off, y_off) {
	ctr = 0;
	console.log("Coords for '" + txt + "':");
  
	//ball radius, uint32 for speed
  var i, radius = 5, data32;       
  
  //clear coords array
  coords = [];         
  
  //clear canvas so we can draw the text
  ctx.clearRect(0, 0, w, h);                        
  
  //draw the text: default 10px at offset (30, 30)
  ctx.fillText(txt.toUpperCase(), x_off, y_off);  
  
  //get uint32 representation of the bitmap:
  data32 = new Uint32Array(ctx.getImageData(0, 0, w, h).data.buffer);
  
  //console.log(data32.length);
  
  //loop through each pixel. We will only get coords of ones w/alpha = 255
  for (i = 0; i < data32.length /*should be 2462400*/; i++) {
  	//check alpha mask
    if (data32[i] & 0xff000000) {
    	filled.push(i);
    	//add new coord if solid pixel           
      coords.push({
      	//x coord should current index in pixel array mod screen width
        x: (i % w),
        
        //can get y coord from curr index div by screen width, floored
        y: ((i / w | 0)), //using any bitwise ops will convert float to int
      });
      
      //console.log("(" + coords[ctr].x + ", " + coords[ctr++].y + ")");
    }
  }
  
  //console.log(coords)
  
  let coordstring = "";
  
  for (i = 0; i < coords.length; i++) {
  	//coordstring = coordstring.concat("(" + coords[i].x + ", " + coords[i].y + "),");
    coordstring = coordstring.concat("{" + coords[i].x + ", " + coords[i].y + "},");
  }
  console.log(coordstring);
}