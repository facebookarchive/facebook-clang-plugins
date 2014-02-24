#include "../ATDWriter.h"

typedef ATDWriter::YojsonWriter<std::ostream> YojsonWriter;
typedef YojsonWriter::ObjectScope ObjectScope;
typedef YojsonWriter::ArrayScope ArrayScope;
typedef YojsonWriter::VariantScope VariantScope;
typedef YojsonWriter::TupleScope TupleScope;

typedef ATDWriter::JsonWriter<std::ostream> JsonWriter;
typedef JsonWriter::ObjectScope STDObjectScope;
typedef JsonWriter::ArrayScope STDArrayScope;
typedef JsonWriter::VariantScope STDVariantScope;
typedef JsonWriter::TupleScope STDTupleScope;

int main(int argc, char **argv) {

  {
    YojsonWriter OF(std::cout);
    OF.emitInteger(100000);
  }
  {
    YojsonWriter OF(std::cout);
    OF.emitString("Hello");
  }
  {
    YojsonWriter OF(std::cout);
    OF.emitBoolean(true);
  }
  {
    YojsonWriter OF(std::cout);
    ArrayScope Scope(OF);
    OF.emitString("Hello");
    OF.emitBoolean(true);
    OF.emitInteger(100000);
  }
  {
    YojsonWriter OF(std::cout);
    ObjectScope Scope(OF);
    OF.emitTag("string");
    OF.emitString("Hello");
    OF.emitTag("boolean");
    OF.emitBoolean(true);
    OF.emitTag("integer");
    OF.emitInteger(100000);
  }
  {
    YojsonWriter OF(std::cout);
    ObjectScope Scope(OF);
    OF.emitTag("integer");
    OF.emitInteger(100000);
    OF.emitTag("array");
    {
      ArrayScope Scope(OF);
      OF.emitInteger(1);
      OF.emitInteger(2);
    }
  }
  {
    JsonWriter OF(std::cout);
    STDTupleScope Scope(OF);
    OF.emitSimpleVariant("zero");
    {
      STDVariantScope Scope(OF, "succ");
      {
        STDVariantScope Scope(OF, "pred");
        OF.emitSimpleVariant("zero");
      }
    }
  }
  {
    YojsonWriter OF(std::cout);
    TupleScope Scope(OF);
    OF.emitSimpleVariant("zero");
    {
      VariantScope Scope(OF, "succ");
      {
        VariantScope Scope(OF, "pred");
        {
	  VariantScope Scope(OF, "eval");
	  {
	    TupleScope Scope(OF);
	    OF.emitString("f");
	    OF.emitString("\"3\t4\n\"");
	  }
	}
      }
    }
  }

  return 0;
}
