function result = H2(d)
    if d==0 || d==1
        result=0;
    else
        result=-d*log2(d)-(1-d)*log2(1-d);
    end
end
