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

var HeifDecoder = function() {
};

HeifDecoder.prototype.decode = function(buffer) {
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
        return [];
    }

    var images = meta_box.get_images(buffer);
    if (!images) {
        return [];
    }
    var result = [];
    var size = images.size();
    for (var i = 0; i < size; i++) {
        result.push(new Uint8Array(StringToArrayBuffer(images.get(i))));
    }
    return result;
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
