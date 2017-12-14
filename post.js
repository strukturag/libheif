var CanvasDrawer = function(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext("2d");
    this.image_data = null;
};

CanvasDrawer.prototype.draw = function(image) {
    var w = image.get_width();
    var h = image.get_height();
    if (w != this.canvas.width || h != this.canvas.height ||
            !this.image_data) {
        this.canvas.width = w;
        this.canvas.height = h;
        this.image_data = this.ctx.createImageData(w, h);
        var image_data = this.image_data.data;
        // Start with a white image.
        for (var i=0; i<w*h; i++) {
            image_data[i*4+3] = 255;
        }
    }

    image.display(this.image_data, function(display_image_data) {
        if (window.requestAnimationFrame) {
            this.pending_image_data = display_image_data;
            window.requestAnimationFrame(function() {
                if (this.pending_image_data) {
                    this.ctx.putImageData(this.pending_image_data, 0, 0);
                    this.pending_image_data = null;
                }
            }.bind(this));
        } else {
            this.ctx.putImageData(display_image_data, 0, 0);
        }
    }.bind(this));
};

function StringToArrayBuffer(str) {
    var buf = new ArrayBuffer(str.length);
    var bufView = new Uint8Array(buf);
    for (var i=0, strLen=str.length; i<strLen; i++) {
        bufView[i] = str.charCodeAt(i);
    }
    return buf;
}

var HeifDecoder = function(libde265) {
    this.libde265 = libde265;
};

HeifDecoder.prototype.decode = function(buffer, image_callback, callback) {
    var input = new libheif.BitstreamRange(buffer);
    var box;
    var meta_box;
    while (true) {
        box = libheif.Box.read(input);
        if (!box || input.error()) {
            break;
        }

        console.log(box.dump());
        if (box.get_short_type() == libheif.fourcc("meta")) {
            meta_box = box;
        }
    }

    if (!meta_box) {
        console.log("No meta box found");
        return false;
    }

    var iloc_box = meta_box.get_child_box(libheif.fourcc("iloc"));
    if (!iloc_box) {
        console.log("No iloc box found");
        return false;
    }
    var iprp_box = meta_box.get_child_box(libheif.fourcc("iprp"));
    if (!iprp_box) {
        console.log("No iprp box found");
        return false;
    }
    var ipco_box = iprp_box.get_child_box(libheif.fourcc("ipco"));
    if (!ipco_box) {
        console.log("No ipco box found");
        return false;
    }
    var hvcC_box = ipco_box.get_child_box(libheif.fourcc("hvcC"));
    if (!hvcC_box) {
        console.log("No hvcC box found");
        return false;
    }

    var pending_buffers = [];
    var hevc_headers = hvcC_box.get_headers();
    if (hevc_headers) {
        pending_buffers.push(new Uint8Array(StringToArrayBuffer(hevc_headers)));
    }
    var hevc_data = iloc_box.read_all_data(buffer);
    if (hevc_data) {
        pending_buffers.push(new Uint8Array(StringToArrayBuffer(hevc_data)));
    }
    if (!pending_buffers.length) {
        console.log("No data to decode");
        return false;
    }

    var hevc_decoder = new this.libde265.Decoder();
    hevc_decoder.disable_filters(true);

    hevc_decoder.set_image_callback(function(image) {
        image_callback(image);
    });

    var do_decode = function() {
        console.log("Decoding");
        hevc_decoder.decode(function(error) {
            switch (error) {
                case this.libde265.DE265_OK:
                    if (!hevc_decoder.has_more()) {
                        callback(error);
                        return;
                    }
                    break;
                case this.libde265.DE265_ERROR_WAITING_FOR_INPUT_DATA:
                    if (pending_buffers === null) {
                        // No more data available to decode.
                        callback(error);
                        return;
                    } else if (pending_buffers.length) {
                        // Pass more data to decoder.
                        hevc_decoder.push_data(pending_buffers.shift(), 0);
                    } else {
                        // All data has been passed.
                        pending_buffers = null;
                        hevc_decoder.flush();
                    }
                    break;
                default:
                    console.log("Error while decoding", error);
                    callback(error);
                    return;
            }
            setTimeout(do_decode, 0);
        });
    };
    // Start decoding deferred.
    setTimeout(do_decode, 0);
    return true;
};

var libheif = {
    // Expose high-level API.
    /** @expose */
    CanvasDrawer: CanvasDrawer,
    /** @expose */
    HeifDecoder: HeifDecoder,

    // Expose low-level API.
    /** @expose */
    fourcc: function(s) {
        return s.charCodeAt(0) << 24 |
            s.charCodeAt(1) << 16 |
            s.charCodeAt(2) << 8 |
            s.charCodeAt(3);
    },
    /** @expose */
    BitstreamRange: Module.BitstreamRange,
    /** @expose */
    Box: Module.Box
};

// don't pollute the global namespace
delete this['Module'];

// On IE this function is called with "undefined" as first parameter. Override
// with a version that supports this behaviour.
function createNamedFunction(name, body) {
    if (!name) {
      name = "function_" + (new Date());
    }
    name = makeLegalFunctionName(name);
    /*jshint evil:true*/
    return new Function(
        "body",
        "return function " + name + "() {\n" +
        "    \"use strict\";" +
        "    return body.apply(this, arguments);\n" +
        "};\n"
    )(body);
}

var root = this;

if (typeof exports !== 'undefined') {
    if (typeof module !== 'undefined' && module.exports) {
        /** @expose */
        exports = module.exports = libheif;
    }
    /** @expose */
    exports.libheif = libheif;
} else {
    /** @expose */
    root.libheif = libheif;
}

if (typeof define === "function" && define.amd) {
    /** @expose */
    define([], function() {
        return libheif;
    });
}

// NOTE: wrapped inside "(function() {" block from pre.js
}).call(this);
